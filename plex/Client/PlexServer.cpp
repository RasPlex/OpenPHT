
#include "Client/PlexServer.h"
#include "utils/XBMCTinyXML.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "threads/SingleLock.h"
#include "PlexConnection.h"
#include "Utility/PlexTimer.h"
#include "PlexTranscoderClient.h"

#include <boost/foreach.hpp>
#include <boost/timer/timer.hpp>
#include <settings/AdvancedSettings.h>

using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexServerConnTestThread::CPlexServerConnTestThread(CPlexConnectionPtr conn, CPlexServerPtr server)
  : CThread("ConnectionTest: " + conn->GetAddress().GetHostName()), m_conn(conn), m_server(server)
{
  Create(true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexServerConnTestThread::Process()
{
  CPlexTimer t;

  if (!m_conn->IsLocal())
  {
    // Delay for 50 ms to make sure we select a local connection from plex.tv first if possible
    CLog::Log(LOGDEBUG, "CPlexServerConnTestThread::Process delaying 50ms for connection %s", m_conn->toString().c_str());
    Sleep(50);
  }

  CPlexConnection::ConnectionState state = m_conn->TestReachability(m_server);

  if (state == CPlexConnection::CONNECTION_STATE_REACHABLE)
    CLog::Log(LOGDEBUG, "CPlexServerConnTestJob:DoWork took %lld sec, Connection SUCCESS %s ~ localConn: %s conn: %s",
              t.elapsed(), m_server->GetName().c_str(), m_conn->IsLocal() ? "YES" : "NO", m_conn->GetAddress().Get().c_str());
  else if (state == CPlexConnection::CONNECTION_STATE_UNKNOWN)
    CLog::Log(LOGDEBUG, "CPlexServerConnTestJob:DoWork took %lld sec, Connection ABORTED %s ~ localConn: %s conn: %s",
              t.elapsed(), m_server->GetName().c_str(), m_conn->IsLocal() ? "YES" : "NO", m_conn->GetAddress().Get().c_str());
  else
    CLog::Log(LOGDEBUG, "CPlexServerConnTestJob:DoWork took %lld sec, Connection FAILURE %s ~ localConn: %s conn: %s",
              t.elapsed(), m_server->GetName().c_str(), m_conn->IsLocal() ? "YES" : "NO", m_conn->GetAddress().Get().c_str());

  m_server->OnConnectionTest(this, m_conn, state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexServerConnTestThread::Cancel()
{
  m_conn->m_http.Cancel();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexServer::CPlexServer(CPlexConnectionPtr connection)
{
  AddConnection(connection);
  m_activeConnection = connection;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexServer::~CPlexServer()
{
  CancelReachabilityTests();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlexServer::CollectDataFromRoot(const CStdString xmlData)
{
  CSingleLock lk(m_serverLock);

  CXBMCTinyXML doc;
  doc.Parse(xmlData);
  if (doc.RootElement() != 0)
  {
    TiXmlElement* root = doc.RootElement();
    bool boolValue;
    std::string uuid;

    /* first we need to check that this is the server we should talk to */
    if (root->QueryStringAttribute("machineIdentifier", &uuid) == TIXML_SUCCESS)
    {
      if (!m_uuid.Equals(uuid.c_str()))
      {
        CLog::Log(LOGWARNING, "CPlexServer::CollectDataFromRoot we wanted to talk to %s but got %s, dropping this connection.", m_uuid.c_str(), uuid.c_str());
        return false;
      }
    }

    if (root->QueryBoolAttribute("allowChannelAccess", &boolValue) == TIXML_SUCCESS)
      m_allowChannelAccess = boolValue;
    else
      m_allowChannelAccess = !IsShared();

    if (root->QueryBoolAttribute("allowMediaDeletion", &boolValue) == TIXML_SUCCESS)
      m_supportsDeletion = boolValue;
    else
      m_supportsDeletion = false;

    if (root->QueryBoolAttribute("transcoderAudio", &boolValue) == TIXML_SUCCESS)
      m_supportsAudioTranscoding = boolValue;
    else
      m_supportsAudioTranscoding = false;

    if (root->QueryBoolAttribute("transcoderVideo", &boolValue) == TIXML_SUCCESS)
      m_supportsVideoTranscoding = boolValue;
    else
      m_supportsVideoTranscoding = false;

    root->QueryStringAttribute("serverClass", &m_serverClass);
    root->QueryStringAttribute("version", &m_version);

    CStdString stringValue;
    if (root->QueryStringAttribute("transcoderVideoResolutions", &stringValue) == TIXML_SUCCESS)
      m_transcoderResolutions = StringUtils::Split(stringValue, ",");

    if (root->QueryStringAttribute("transcoderVideoBitrates", &stringValue) == TIXML_SUCCESS)
      m_transcoderBitrates = StringUtils::Split(stringValue, ",");

    // we Add a max value to the transcoder provided ones
    m_transcoderBitrates.push_back(PLEX_TRANSCODER_MAX_BITRATE_STR);

    if (root->QueryStringAttribute("transcoderVideoQualities", &stringValue) == TIXML_SUCCESS)
      m_transcoderQualities = StringUtils::Split(stringValue, ",");

    CLog::Log(LOGDEBUG, "CPlexServer::CollectDataFromRoot knowledge complete: %s", toString().c_str());
  }
  else
  {
    CLog::Log(LOGWARNING, "CPlexServer::CollectDataFromRoot parser fail!");
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlexServer::HasActiveLocalConnection() const
{
  CSingleLock lk(m_serverLock);
  return (m_activeConnection != NULL && m_activeConnection->IsLocal());
}


///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexConnectionPtr CPlexServer::GetLocalConnection() const
{
  if (HasActiveLocalConnection())
    return m_activeConnection;

  CSingleLock lk(m_serverLock);
  BOOST_FOREACH(CPlexConnectionPtr connection, m_connections)
  {
    if (connection->IsLocal())
      return connection;
  }
  return CPlexConnectionPtr();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexServer::MarkAsRefreshing()
{
  CSingleLock lk(m_serverLock);
  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
    conn->SetRefreshed(false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlexServer::MarkUpdateFinished(int connType)
{
  vector<CPlexConnectionPtr> connsToRemove;
  
  CSingleLock lk(m_serverLock);

  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
  {
    bool disconnected = false;
    if (conn->GetRefreshed() == false)
    {
      if ((conn->m_type & CPlexConnection::CONNECTION_DISCOVERED) == CPlexConnection::CONNECTION_DISCOVERED &&
          (connType & CPlexConnection::CONNECTION_DISCOVERED) == CPlexConnection::CONNECTION_DISCOVERED)
        disconnected = true;
      conn->m_type &= ~connType;
      if ((connType & CPlexConnection::CONNECTION_MYPLEX) == CPlexConnection::CONNECTION_MYPLEX && !conn->GetAccessToken().empty())
      {
        // When we remove a MyPlex connection type and still have a token we need to clear
        // out the token to make sure it won't linger on the local connection
        //
        conn->SetAccessToken("");
      }
    }

    if (conn->m_type == 0)
      connsToRemove.push_back(conn);
    else if (disconnected && m_activeConnection && m_activeConnection->Equals(conn))
    {
      CLog::Log(LOGDEBUG, "CPlexServer::MarkUpdateFinished Lost activeConnection for server %s", GetName().c_str());
      m_activeConnection.reset();
    }
  }

  BOOST_FOREACH(CPlexConnectionPtr conn, connsToRemove)
  {
    CLog::Log(LOGDEBUG, "CPlexServer::MarkUpdateFinished Removing connection for %s after update finished for type %d: %s",
              m_name.c_str(), connType, conn->toString().c_str());
    vector<CPlexConnectionPtr>::iterator it = find(m_connections.begin(), m_connections.end(), conn);
    m_connections.erase(it);

    if (m_activeConnection && m_activeConnection->Equals(conn))
    {
      CLog::Log(LOGDEBUG, "CPlexServer::MarkUpdateFinished Lost activeConnection for server %s", GetName().c_str());
      m_activeConnection.reset();
    }
  }

  return m_connections.size() > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool ConnectionSortFunction(CPlexConnectionPtr c1, CPlexConnectionPtr c2)
{
  if (c1->IsLocal() && !c2->IsLocal()) return true;
  if (!c1->IsLocal() && c2->IsLocal()) return false;
  return c1->GetAddress().Get() < c2->GetAddress().Get();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlexServer::UpdateReachability()
{
  if (m_connections.size() == 0)
    return false;

  boost::timer::cpu_timer timer; // start timer
  CLog::Log(LOGDEBUG, "CPlexServer::UpdateReachability Updating reachability for %s with %ld connections.", m_name.c_str(), m_connections.size());

  m_bestConnection.reset();
  m_connectionsLeft = m_connections.size();
  m_complete = false;

  vector<CPlexConnectionPtr> sortedConnections = m_connections;
  sort(sortedConnections.begin(), sortedConnections.end(), ConnectionSortFunction);

  if (m_connTestThreads.size() > 0)
  {
    CancelReachabilityTests();
  }
  
  CSingleLock lk(m_connTestThreadLock);
  m_testEvent.Reset();

  BOOST_FOREACH(CPlexConnectionPtr conn, sortedConnections)
  {
    CLog::Log(LOGDEBUG, "CPlexServer::UpdateReachability testing connection %s", conn->toString().c_str());
    if ((g_plexApplication.myPlexManager && g_plexApplication.myPlexManager->GetCurrentUserInfo().restricted && conn->GetAccessToken().IsEmpty()))
    {
      CLog::Log(LOGINFO, "CPlexServer::UpdateReachability skipping connection %s since we are restricted", conn->toString().c_str());
      m_connectionsLeft --;
      continue;
    }
    else if (g_advancedSettings.m_bRequireEncryptedConnection && conn->isSSL() == false)
    {
      CLog::Log(LOGINFO, "CPlexServer::UpdateReachability skipping connection %s since it's not encrypted", conn->toString().c_str());
      m_connectionsLeft --;
      continue;
    }
    
    m_connTestThreads.push_back(new CPlexServerConnTestThread(conn, GetShared()));
  }
  lk.unlock();

  /* Three minutes should be enough ? */
  if (m_connectionsLeft != 0)
  {
    if (!m_testEvent.WaitMSec(1000 * 120))
      CLog::Log(LOGWARNING, "CPlexServer::UpdateReachability waited 2 minutes and connection testing didn't finish.");
  }

  CSingleLock tlk(m_testingLock);
  m_complete = true;
  m_activeConnection = m_bestConnection;
  timer.stop();

  CLog::Log(LOGDEBUG, "CPlexServer::UpdateReachability Connectivity test to %s completed in %s Seconds -> %s",
            m_name.c_str(), timer.format(1, "%w").c_str(), m_activeConnection ? m_activeConnection->toString().c_str() : "FAILED");

  return (bool)m_bestConnection;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexServer::CancelReachabilityTests()
{
  CSingleLock lk(m_connTestThreadLock);

  m_noMoreConnThreads.Reset();
  
  if (m_connTestThreads.size())
  {
    BOOST_FOREACH(CPlexServerConnTestThread* thread, m_connTestThreads)
      thread->Cancel();
    lk.Leave();
    
    m_noMoreConnThreads.WaitMSec(3000);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CStdString CPlexServer::GetAccessToken() const
{
  CSingleLock lk(m_serverLock);
  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
  {
    if (!conn->GetAccessToken().empty())
      return conn->GetAccessToken();
  }
  return CStdString();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexServer::OnConnectionTest(CPlexServerConnTestThread* thread, CPlexConnectionPtr conn,
                                   int state)
{
  if (thread)
  {
    CSingleLock lk(m_connTestThreadLock);
    if (std::find(m_connTestThreads.begin(), m_connTestThreads.end(), thread) != m_connTestThreads.end())
      m_connTestThreads.erase(std::remove(m_connTestThreads.begin(),m_connTestThreads.end(), thread),
                              m_connTestThreads.end());
    else
      return;
  }

  CSingleLock tlk(m_testingLock);
  if (state == CPlexConnection::CONNECTION_STATE_REACHABLE)
  {
    if (!m_bestConnection)
    {
      CLog::Log(LOGDEBUG, "CPlexServer::OnConnectionTest setting bestConnection on %s to %s", GetName().c_str(), conn->GetAddress().Get().c_str());
      m_bestConnection = conn;

      if (!m_complete)
        m_testEvent.Set();
    }
    else
    {
      bool isBetterLocal = !m_bestConnection->IsLocal() && conn->IsLocal();
      bool isBetterSSL = !m_bestConnection->isSSL() && conn->isSSL();

      if ((isBetterSSL && conn->IsLocal()) || isBetterLocal)
      {
        CLog::Log(LOGDEBUG, "CPlexServer::OnConnectionTest found better connection on %s to %s", GetName().c_str(), conn->GetAddress().Get().c_str());
        m_bestConnection = m_activeConnection = conn;
      }
    }
  }

  if (--m_connectionsLeft == 0 && m_complete == false)
  {
    m_testEvent.Set();
  }
  
  if (m_connTestThreads.size() == 0)
    m_noMoreConnThreads.Set();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexServer::Merge(CPlexServerPtr otherServer)
{
  CSingleLock lk(m_serverLock);
  CSingleLock tlk(m_testingLock);

  m_name = otherServer->m_name;
  if (!otherServer->m_version.empty())
    m_version = otherServer->m_version;

  // Token ownership is the only ownership metric to be believed. Everything else defaults to owned.
  // If something comes after myPlex on the LAN, say, we'll reset ownership to owned.
  if (!otherServer->GetAccessToken().empty())
    m_owned = otherServer->m_owned;

  if (!otherServer->GetOwner().empty())
    m_owner = otherServer->m_owner;
  
  if (otherServer->GetHome() || m_home)
    m_home = true;

  BOOST_FOREACH(CPlexConnectionPtr conn, otherServer->m_connections)
  {
    if (conn->IsReachable())
      m_allowChannelAccess = otherServer->m_allowChannelAccess;

    bool found = false;
    BOOST_FOREACH(CPlexConnectionPtr mappedConn, m_connections)
    {
      if (conn->Equals(mappedConn))
      {
        mappedConn->Merge(conn);
        found = true;
        if (otherServer->m_activeConnection == conn && (!m_activeConnection || !m_activeConnection->IsLocal() && mappedConn->IsLocal()))
        {
          CLog::Log(LOGDEBUG, "CPlexServer::Merge found better connection on %s to %s", m_name.c_str(), mappedConn->GetAddress().Get().c_str());
          m_bestConnection = m_activeConnection = mappedConn;
        }
        break;
      }
    }

    if (!found)
    {
      AddConnection(conn);
      if (otherServer->m_activeConnection == conn && (!m_activeConnection || !m_activeConnection->IsLocal() && conn->IsLocal()))
      {
        CLog::Log(LOGDEBUG, "CPlexServer::Merge found better connection on %s to %s", m_name.c_str(), conn->GetAddress().Get().c_str());
        m_bestConnection = m_activeConnection = conn;
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexServer::GetConnections(std::vector<CPlexConnectionPtr> &conns)
{
  conns = m_connections;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
int CPlexServer::GetNumConnections() const
{
  return m_connections.size();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexConnectionPtr CPlexServer::GetActiveConnection() const
{
  return m_activeConnection;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CURL CPlexServer::GetActiveConnectionURL() const
{
  if (!m_activeConnection)
    return CURL();

  return m_activeConnection->GetAddress();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CURL CPlexServer::BuildPlexURL(const CStdString& path) const
{
  if (m_uuid.empty() == false)
  {
    CURL url;
    url.SetProtocol("plexserver");
    url.SetHostName(m_uuid);
    url.SetFileName(path);
    return url.Get();
  }
  else
  {
    // this means that we have a synthesized server without a uuid
    // so we can't use plexserver:// url's. Maybe try to return a
    // real URL instead?
    return BuildURL(path);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CURL CPlexServer::BuildURL(const CStdString &path, const CStdString &options) const
{
  CPlexConnectionPtr connection = m_activeConnection;

  if (!connection)
  {
    CLog::Log(LOGDEBUG, "CPlexServer::BuildURL no active connections for %s", toString().c_str());
    return CURL();
  }

  CURL url = connection->BuildURL(path);

  if (!options.empty())
    url.SetOptions(options);

  if (!url.HasOption(connection->GetAccessTokenParameter()))
  {
    /* See if we can find a token in our other connections */
    CStdString token;
    BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
    {
      if (!conn->GetAccessToken().IsEmpty())
      {
        token = conn->GetAccessToken();
        url.SetOption(connection->GetAccessTokenParameter(), token);
        break;
      }
    }
  }
  return url;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlexServer::HasAuthToken() const
{
  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
  {
    if (!conn->GetAccessToken().IsEmpty())
      return true;
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
string CPlexServer::GetAnyToken() const
{
  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
  {
    if (!conn->GetAccessToken().IsEmpty())
      return conn->GetAccessToken();
  }
  return string();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexServer::AddConnection(CPlexConnectionPtr connection)
{
  m_connections.push_back(connection);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CStdString CPlexServer::toString() const
{
  CStdString ret;
  ret.Format("%s version: %s activeConnection: %s owner: %s deletion: %s class: %s",
             m_name,
             m_version,
             m_activeConnection ? m_activeConnection->GetAddress().GetHostName() : "NO",
             m_owner.empty() ? "you" : m_owner,
             m_supportsDeletion ? "YES" : "NO",
             m_serverClass);

  return ret;
}
