
#include "PlexPubsubManager.h"
#include "settings/GUISettings.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "Client/MyPlex/MyPlexManager.h"
#include "Client/MyPlex/MyPlexUserInfo.h"
#include "Remote/PlexHTTPRemoteHandler.h"
#include "PlexApplication.h"
#include "Application.h"

CPlexPubsubManager::CPlexPubsubManager()
  : m_bStop(true), m_event(false)
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);

  m_client.clear_access_channels(websocketpp::log::alevel::all);
  m_client.clear_error_channels(websocketpp::log::elevel::all);

  m_client.init_asio();

  m_client.set_tls_init_handler(websocketpp::lib::bind(&CPlexPubsubManager::OnTlsInit, this, websocketpp::lib::placeholders::_1));
  m_client.set_open_handler(websocketpp::lib::bind(&CPlexPubsubManager::OnOpen, this, websocketpp::lib::placeholders::_1));
  m_client.set_close_handler(websocketpp::lib::bind(&CPlexPubsubManager::OnClose, this, websocketpp::lib::placeholders::_1));
  m_client.set_fail_handler(websocketpp::lib::bind(&CPlexPubsubManager::OnFail, this, websocketpp::lib::placeholders::_1));
  m_client.set_message_handler(websocketpp::lib::bind(&CPlexPubsubManager::OnMessage, this, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));
}

CPlexPubsubManager::~CPlexPubsubManager()
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);
}

void CPlexPubsubManager::Start()
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);

  CMyPlexUserInfo info = g_plexApplication.myPlexManager->GetCurrentUserInfo();
  if (info.id > 0)
  {
    CLog::Log(LOGINFO, "CPlexPubsubManager::%s - Connecting to pubsub.plex.tv for user %i", __FUNCTION__, info.id);
    m_uri = StringUtils::Format("wss://pubsub.plex.tv/sub/websockets/%i/%s?X-Plex-Token=%s", info.id, g_guiSettings.GetString("system.uuid").c_str(), info.authToken.c_str());
    m_bStop = false;
  }
  else
  {
    CLog::Log(LOGINFO, "CPlexPubsubManager::%s - Disconnecting from pubsub.plex.tv", __FUNCTION__);
    Stop();
    return;
  }

  if (!m_thread)
    m_thread = websocketpp::lib::make_shared<websocketpp::lib::thread>(&CPlexPubsubManager::Run, this);
}

void CPlexPubsubManager::Stop()
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);

  m_bStop = true;
  m_event.Set();

  if (!m_hdl.expired())
  {
    websocketpp::lib::error_code ec;
    m_client.close(m_hdl, websocketpp::close::status::going_away, "", ec);
    if (ec)
      CLog::Log(LOGERROR, "CPlexPubsubManager::%s - Error closing connection: %s", __FUNCTION__, ec.message().c_str());
  }

  m_client.stop();

  if (m_thread)
    m_thread->join();
  m_thread.reset();
}

void CPlexPubsubManager::Run()
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);

  while (!m_bStop)
  {
    CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - Connecting", __FUNCTION__);

    m_client.reset();

    websocketpp::lib::error_code ec;
    CPlexWebsocketClient::connection_ptr con = m_client.get_connection(m_uri, ec);
    if (ec)
    {
      CLog::Log(LOGERROR, "CPlexPubsubManager::%s - Connection Error: %s", __FUNCTION__, ec.message().c_str());
      return;
    }

    m_hdl = con->get_handle();
    m_client.connect(con);

    m_client.run();

    CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - Ended", __FUNCTION__);

    m_event.WaitMSec(30000);
  }
}

CPlexWebsocketContextPtr CPlexPubsubManager::OnTlsInit(websocketpp::connection_hdl hdl)
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);

  boost::system::error_code ec;
  CPlexWebsocketContextPtr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

  ctx->set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::single_dh_use, ec);
  if (ec)
    CLog::Log(LOGERROR, "CPlexPubsubManager::%s - Error init tls: %s", __FUNCTION__, ec.message().c_str());
  return ctx;
}

void CPlexPubsubManager::OnOpen(websocketpp::connection_hdl hdl)
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);
}

void CPlexPubsubManager::OnFail(websocketpp::connection_hdl hdl)
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);

  CPlexWebsocketClient::connection_ptr con = m_client.get_con_from_hdl(hdl);
  CLog::Log(LOGWARNING, "CPlexPubsubManager::%s - Failed: %s", __FUNCTION__, con->get_ec().message().c_str());
}

void CPlexPubsubManager::OnClose(websocketpp::connection_hdl hdl)
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);

  CPlexWebsocketClient::connection_ptr con = m_client.get_con_from_hdl(hdl);
  std::stringstream s;
  s << "close code: " << con->get_remote_close_code() << " ("
    << websocketpp::close::status::get_string(con->get_remote_close_code())
    << "), close reason: " << con->get_remote_close_reason();

  CLog::Log(LOGINFO, "CPlexPubsubManager::%s - Closed: %s", __FUNCTION__, s.str().c_str());
}

void CPlexPubsubManager::OnMessage(websocketpp::connection_hdl, CPlexWebsocketClient::message_ptr msg)
{
  CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - called", __FUNCTION__);

  if (msg->get_opcode() == websocketpp::frame::opcode::text)
  {
    CXBMCTinyXML doc;
    doc.Parse(msg->get_payload().c_str());

    if (!doc.Error() && doc.RootElement() != NULL && doc.RootElement()->ValueStr() == "Message" && !strcmp(doc.RootElement()->Attribute("command"), "processRemoteControlCommand"))
    {
      for (TiXmlElement *command = doc.RootElement()->FirstChildElement("Command"); command; command = command->NextSiblingElement("Command"))
      {
        std::string commandID, path, clientIdentifier;
        command->QueryStringAttribute("commandID", &commandID);
        command->QueryStringAttribute("path", &path);
        command->QueryStringAttribute("clientIdentifier", &clientIdentifier);

        CLog::Log(LOGINFO, "CPlexPubsubManager::%s - Command: %s", __FUNCTION__, path.c_str());

        if (!commandID.empty() && !path.empty() && !clientIdentifier.empty())
        {
          ArgMap arguments;
          for (TiXmlAttribute *attribute = command->FirstAttribute(); attribute; attribute = attribute->Next())
          {
            std::string name = attribute->NameTStr();
            if (StringUtils::StartsWith(name, "query"))
            {
              std::string key = name.substr(5, 1);
              StringUtils::ToLower(key);
              key += name.substr(6);
              arguments[key] = attribute->ValueStr();

              if (key != "token")
                CLog::Log(LOGDEBUG, "CPlexPubsubManager::%s - Query: %s=%s", __FUNCTION__, key.c_str(), attribute->Value());
            }
          }
          g_application.m_plexRemoteHandler.HandleRemoteCommand(path, arguments);
        }
      }
    }
  }
}
