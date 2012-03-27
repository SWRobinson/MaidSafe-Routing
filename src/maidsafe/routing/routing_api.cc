/*******************************************************************************
 *  Copyright 2012 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 ******************************************************************************/

#include <utility>

#include "maidsafe/common/utils.h"
#include "maidsafe/routing/routing_api.h"
#include "maidsafe/routing/routing.pb.h"
#include "maidsafe/routing/node_id.h"
#include "maidsafe/routing/routing_table.h"
#include "maidsafe/routing/timer.h"
#include "maidsafe/routing/bootstrap_file_handler.h"
#include "maidsafe/routing/return_codes.h"
#include "maidsafe/routing/utils.h"
#include "maidsafe/routing/message_handler.h"
#include "maidsafe/routing/parameters.h"

namespace fs = boost::filesystem;
namespace bs2 = boost::signals2;

namespace maidsafe {

namespace routing {

Message::Message()
    : type(0),
      source_id(),
      destination_id(),
      data(),
      timeout(Parameters::timout_in_seconds),
      direct(false),
      replication(1) {}

Message::Message(const protobuf::Message &protobuf_message)
    : type(protobuf_message.type()),
      source_id(protobuf_message.source_id()),
      destination_id(protobuf_message.destination_id()),
      data(protobuf_message.data()),
      timeout(Parameters::timout_in_seconds),
      direct(protobuf_message.direct()),
      replication(protobuf_message.replication()) {}

Routing::Routing(const asymm::Keys &keys)
    : asio_service_(),
      bootstrap_nodes_(),
      keys_(keys),
      node_local_endpoint_(),
      node_external_endpoint_(),
      transport_(),
      routing_table_(new RoutingTable(keys_)),
      timer_(new Timer(asio_service_)),
      message_handler_(),
      message_received_signal_(),
      network_status_signal_(),
      close_node_from_to_signal_(),
      waiting_for_response_(),
      client_connections_(),
      client_routing_table_(),
      joined_(false),
      node_validation_functor_()
{
  Parameters::client_mode = false;
  Init();
}

Routing::Routing()
    : asio_service_(),
      bootstrap_nodes_(),
      keys_(),
      node_local_endpoint_(),
      node_external_endpoint_(),
      transport_(),
      routing_table_(new RoutingTable(keys_)),
      timer_(new Timer(asio_service_)),
      message_handler_(),
      message_received_signal_(),
      network_status_signal_(),
      close_node_from_to_signal_(),
      waiting_for_response_(),
      client_connections_(),
      client_routing_table_(),
      joined_(false),
      node_validation_functor_()
{
  Parameters::client_mode = true;
  Init();
}

// drop existing routing table and restart
void Routing::BootStrapFromThisEndpoint(const transport::Endpoint
&endpoint) {
  LOG(INFO) << " Entered bootstrap IP address : " << endpoint.ip.to_string();
  LOG(INFO) << " Entered bootstrap Port       : " << endpoint.port;
  for (unsigned int i = 0; i < routing_table_->Size(); ++i) {
    NodeInfo remove_node =
    routing_table_->GetClosestNode(NodeId(routing_table_->kKeys().identity), 0);
    transport_.Remove(remove_node.endpoint);
    routing_table_->DropNode(remove_node.endpoint);
  }
  network_status_signal_(routing_table_->Size());
  bootstrap_nodes_.clear();
  bootstrap_nodes_.push_back(endpoint);
  asio_service_.service().post(std::bind(&Routing::Join, this));
}

bool Routing::SetEncryption(bool encryption_required) {
  return (Parameters::encryption_required = encryption_required);
}

bool Routing::SetCompanyName(const std::string &company) const {
  if (company.empty()) {
    DLOG(ERROR) << "tried to set empty company name";
    return false;
  }
  Parameters::company_name = company;
  return (Parameters::company_name == company);
}

bool Routing::SetApplicationName(const std::string &application_name) const {
  if(application_name.empty()) {
    DLOG(ERROR) << "tried to set empty application name";
    return false;
  }
  Parameters::application_name = application_name;
  return (Parameters::application_name == application_name);

}
// TODO(dirvine) I don not think this should be allowed to be changed
// bool Routing::SetBoostrapFilePath(const boost::filesystem3::path &path) const {
//   if (path.empty()) {
//     DLOG(ERROR) << "tried to set empty bootstrap file path";
//     return false;
//   }
//   Parameters::bootstrap_file_path = path;
//   return (Parameters::bootstrap_file_path == path);
// }

int Routing::Send(const Message &message,
                   const MessageReceivedFunctor response_functor) {
  if (message.destination_id.empty()) {
    DLOG(ERROR) << "No destination id, aborted send";
    return kInvalidDestinatinId;
  }
  if (message.data.empty() && (message.type != 100)) {
    DLOG(ERROR) << "No data, aborted send";
    return kEmptyData;
  }
  if (message.type < 100) {
    DLOG(ERROR) << "Attempt to use Reserved message type (<100), aborted send";
    return kInvalidType;
  }
  uint32_t message_unique_id =  timer_->AddTask(message.timeout,
                                                response_functor);
  protobuf::Message proto_message;
  proto_message.set_id(message_unique_id);
  proto_message.set_source_id(routing_table_->kKeys().identity);
  proto_message.set_destination_id(message.destination_id);
  proto_message.set_data(message.data);
  proto_message.set_direct(message.direct);
  proto_message.set_replication(message.replication);
  proto_message.set_type(message.type);
  proto_message.set_routing_failure(false);
  SendOn(proto_message, transport_, routing_table_);
  return 0;
}

void Routing::SetNodeValidationFunctor(NodeValidationFunctor
                                       &node_validation_functor) {
  if (!node_validation_functor) {
    DLOG(ERROR) << "Invalid node_validation_functor passed ";
    return;
  }
  node_validation_functor_ = node_validation_functor;
}

void Routing::ValidateThisNode(const std::string &node_id,
                               const asymm::PublicKey &public_key,
                               const transport::Endpoint &endpoint,
                               bool client) {
  NodeInfo node_info;
  node_info.node_id =NodeId(node_id);
  node_info.public_key = public_key;
  node_info.endpoint = endpoint;
  if (client) {
    client_connections_.push_back(node_info);
  } else {
    transport_.Add(endpoint, node_id);
    routing_table_->AddNode(node_info);
    if (bootstrap_nodes_.size() > 1000) {
    bootstrap_nodes_.erase(bootstrap_nodes_.begin());
    }
    bootstrap_nodes_.push_back(endpoint);
    BootStrapFile bfile;
    bfile.WriteBootstrapFile(bootstrap_nodes_);
  }
}

void Routing::Init() {
  if (!node_validation_functor_) {
    DLOG(ERROR) << "Invalid node_validation_functor passed: Aborted start";
    return;
  }
  message_handler_.reset(new MessageHandler(node_validation_functor_,
                                            routing_table_,
                                            transport_,
                                            timer_));
  asio_service_.Start(5);
  node_local_endpoint_ = transport_.GetAvailableEndpoint();
  // TODO(dirvine) connect transport signals !!
  LOG(INFO) << " Local IP address : " << node_local_endpoint_.ip.to_string();
  LOG(INFO) << " Local Port       : " << node_local_endpoint_.port;
  Join();
}

bs2::signal<void(int, std::string)> &Routing::MessageReceivedSignal() {
  return message_received_signal_;
}

bs2::signal<void(unsigned int)> &Routing::NetworkStatusSignal() {
  return network_status_signal_;
}

bs2::signal<void(std::string, std::string)>
                            &Routing::CloseNodeReplacedOldNewSignal() {
  return routing_table_->CloseNodeReplacedOldNewSignal();
}

void Routing::Join() {
  if (bootstrap_nodes_.empty()) {
    DLOG(INFO) << "No bootstrap nodes";
    return;
  }

  for (auto it = bootstrap_nodes_.begin();
       it != bootstrap_nodes_.end(); ++it) {
    // TODO(dirvine) send bootstrap requests
  }
}

void Routing::ReceiveMessage(const std::string &message) {
  protobuf::Message protobuf_message;
  protobuf::ConnectRequest connection_request;
  if (protobuf_message.ParseFromString(message))
    message_handler_->ProcessMessage(protobuf_message);
}

void Routing::ConnectionLost(transport::Endpoint& lost_endpoint) {
  if (!routing_table_->DropNode(lost_endpoint))
    return;
    for (auto it = client_connections_.begin();
         it != client_connections_.end(); ++it) {
       if((*it).endpoint ==  endpoint) {
          client_connections_.erase(it);
          return true;
       }
    }
    for (auto it = client_routing_table_.begin();
         it != client_routing_table_.end(); ++it) {
       if((*it).endpoint ==  endpoint) {
          client_routing_table_.erase(it);
          /// TODO(dirvine) do another find node on ourself
          return true;
       }
    }
}


}  // namespace routing

}  // namespace maidsafe
