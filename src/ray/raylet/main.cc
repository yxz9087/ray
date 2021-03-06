#include <iostream>

#include "ray/raylet/raylet.h"
#include "ray/status.h"

#ifndef RAYLET_TEST
int main(int argc, char *argv[]) {
  RAY_CHECK(argc == 8);

  const std::string raylet_socket_name = std::string(argv[1]);
  const std::string store_socket_name = std::string(argv[2]);
  const std::string node_ip_address = std::string(argv[3]);
  const std::string redis_address = std::string(argv[4]);
  int redis_port = std::stoi(argv[5]);
  const std::string worker_command = std::string(argv[6]);
  const std::string static_resource_list = std::string(argv[7]);

  // Configuration for the node manager.
  ray::raylet::NodeManagerConfig node_manager_config;
  std::unordered_map<std::string, double> static_resource_conf;
  // Parse the resource list.
  std::istringstream resource_string(static_resource_list);
  std::string resource_name;
  std::string resource_quantity;

  while (std::getline(resource_string, resource_name, ',')) {
    RAY_CHECK(std::getline(resource_string, resource_quantity, ','));
    // TODO(rkn): The line below could throw an exception. What should we do about this?
    static_resource_conf[resource_name] = std::stod(resource_quantity);
  }
  node_manager_config.resource_config =
      ray::raylet::ResourceSet(std::move(static_resource_conf));
  RAY_LOG(INFO) << "Starting raylet with static resource configuration: "
                << node_manager_config.resource_config.ToString();
  node_manager_config.num_initial_workers = 0;
  // Use a default worker that can execute empty tasks with dependencies.

  std::stringstream worker_command_stream(worker_command);
  std::string token;
  while (getline(worker_command_stream, token, ' ')) {
    node_manager_config.worker_command.push_back(token);
  }

  node_manager_config.heartbeat_period_ms =
      RayConfig::instance().heartbeat_timeout_milliseconds();

  // Configuration for the object manager.
  ray::ObjectManagerConfig object_manager_config;
  object_manager_config.store_socket_name = store_socket_name;
  // Time out in milliseconds to wait before retrying a failed pull.
  object_manager_config.pull_timeout_ms = 100;
  // Maximum number of sends allowed.
  object_manager_config.max_sends = 2;
  // Maximum number of receives allowed.
  object_manager_config.max_receives = 2;
  // Object chunk size, in bytes.
  object_manager_config.object_chunk_size = static_cast<uint64_t>(std::pow(10, 8));

  //  initialize mock gcs & object directory
  auto gcs_client = std::make_shared<ray::gcs::AsyncGcsClient>();
  RAY_LOG(INFO) << "Initializing GCS client "
                << gcs_client->client_table().GetLocalClientId();

  // Initialize the node manager.
  boost::asio::io_service main_service;
  std::unique_ptr<boost::asio::io_service> object_manager_service;

  object_manager_service.reset(new boost::asio::io_service());
  ray::raylet::Raylet server(main_service, std::move(object_manager_service),
                             raylet_socket_name, node_ip_address, redis_address,
                             redis_port, node_manager_config, object_manager_config,
                             gcs_client);

  // Destroy the Raylet on a SIGTERM. The pointer to main_service is
  // guaranteed to be valid since this function will run the event loop
  // instead of returning immediately.
  auto handler = [&main_service](const boost::system::error_code &error,
                                 int signal_number) { main_service.stop(); };
  boost::asio::signal_set signals(main_service, SIGTERM);
  signals.async_wait(handler);

  main_service.run();
}
#endif
