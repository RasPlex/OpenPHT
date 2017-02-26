#pragma once

#define BOOST_ASIO_DISABLE_IOCP 1; // IOCP reactor reads failed using boost 1.44.

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>

#include "threads/Event.h"

typedef websocketpp::client<websocketpp::config::asio_tls_client> CPlexWebsocketClient;
typedef websocketpp::lib::shared_ptr<boost::asio::ssl::context> CPlexWebsocketContextPtr;

class CPlexPubsubManager
{
public:
  CPlexPubsubManager(void);
  virtual ~CPlexPubsubManager(void);
  void Start();
  void Stop();
private:
  void Run();

  CPlexWebsocketContextPtr OnTlsInit(websocketpp::connection_hdl hdl);
  void OnOpen(websocketpp::connection_hdl hdl);
  void OnFail(websocketpp::connection_hdl hdl);
  void OnClose(websocketpp::connection_hdl hdl);
  void OnMessage(websocketpp::connection_hdl, CPlexWebsocketClient::message_ptr msg);

  bool m_bStop;
  std::string m_uri;
  CPlexWebsocketClient m_client;
  websocketpp::connection_hdl m_hdl;
  websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread;
  CEvent m_event;
};
