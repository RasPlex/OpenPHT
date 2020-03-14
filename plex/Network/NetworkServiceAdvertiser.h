/*
 *  Copyright (C) 2010 Plex, Inc.   
 *
 *  Created on: Dec 16, 2010
 *      Author: Elan Feingold
 */

#pragma once

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#include "NetworkService.h"
#include "NetworkServiceBase.h"

#define CRLF "\r\n"

class NetworkServiceAdvertiser;
typedef boost::shared_ptr<NetworkServiceAdvertiser> NetworkServiceAdvertiserPtr;
typedef pair<udp_socket_ptr, std::string> socket_string_pair;

/////////////////////////////////////////////////////////////////////////////
class NetworkServiceAdvertiser : public NetworkServiceBase
{
 public:
  
  /// Constructor.
  NetworkServiceAdvertiser(boost::asio::io_service& ioService, unsigned short port)
   : NetworkServiceBase(ioService)
   , m_port(port)
  {
    // This is where we'll send notifications.
    m_notifyEndpoint = boost::asio::ip::udp::endpoint(NS_BROADCAST_ADDR, m_port + 1);
  }
  
  /// Destructor.
  virtual ~NetworkServiceAdvertiser() {}
 
  /// Start advertising the service.
  void start()
  {
    doStart();
  }
  
  /// Stop advertising the service.
  void stop()
  {
    doStop();
    
    // Send out the BYE message synchronously and close the sockets.
    dprintf("NetworkService: Stopping advertisement.");
    broadcastMessage("BYE");
    if (m_broadcastSocket)
      m_broadcastSocket->close();
    m_broadcastSocket.reset();
    BOOST_FOREACH(const socket_string_pair& pair, m_sockets)
      pair.first->close();
    m_sockets.clear();
    m_ignoredAddresses.clear();
  }
  
  /// Advertise an update to the service.
  void update(const string& parameter="")
  {
    broadcastMessage("UPDATE", parameter);
  }
  
  /// For subclasses to fill in.
  virtual void createReply(map<string, string>& headers) {}
  
  /// For subclasses to fill in.
  virtual string getType() = 0;
  virtual string getResourceIdentifier() = 0;
  virtual string getBody() = 0;
  
 protected:
  
  virtual void doStart() {}
  virtual void doStop() {}
  
 private:
  
  /// Handle network change.
  virtual void handleNetworkChange(const vector<NetworkInterface>& interfaces)
  {
    dprintf("Network change for advertiser, closing %lu advertiser sockets.", m_sockets.size());

    // Close the old sockets.
    if (m_broadcastSocket)
      m_broadcastSocket->close();
    m_broadcastSocket.reset();
    BOOST_FOREACH(const socket_string_pair& pair, m_sockets)
      pair.first->close();
    m_sockets.clear();
    m_ignoredAddresses.clear();

    // Create new broadcast socket.
    m_broadcastSocket = udp_socket_ptr(new boost::asio::ip::udp::socket(m_ioService));
    setupListener(m_broadcastSocket, "0.0.0.0", m_port);

    // Wait for data.
    m_broadcastSocket->async_receive_from(boost::asio::buffer(m_data, NS_MAX_PACKET_SIZE), m_endpoint, boost::bind(&NetworkServiceAdvertiser::handleRead, this, m_broadcastSocket, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, -1));

    int interfaceIndex = 0;
    BOOST_FOREACH(const NetworkInterface& xface, interfaces)
    {
      // Don't add loopback or virtual interfaces.
      if (!xface.loopback() && xface.name()[0] != 'v')
      {
        string broadcast = xface.broadcast();
        dprintf("NetworkService: Advertising on interface %s on broadcast address %s (index: %d)", xface.address().c_str(), broadcast.c_str(), interfaceIndex);

        // Create new multicast socket for network interface.
        udp_socket_ptr socket = udp_socket_ptr(new boost::asio::ip::udp::socket(m_ioService));
        setupMulticastListener(socket, xface.address(), m_notifyEndpoint.address(), m_port, true);
        m_sockets.push_back(socket_string_pair(socket, broadcast));

        // Wait for data.
        socket->async_receive_from(boost::asio::buffer(m_data, NS_MAX_PACKET_SIZE), m_endpoint, boost::bind(&NetworkServiceAdvertiser::handleRead, this, socket, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, interfaceIndex));
        interfaceIndex++;
      }
      else
      {
        // Sometimes we get packets from these interfaces, not sure why, but we'll ignore them.
        // We get packets from these interfaces because we are listening on 0.0.0.0
        m_ignoredAddresses.insert(xface.address());
      }
    }

    // Always send out the HELLO packet, just in case.
    broadcastMessage("HELLO");
  }

  void broadcastMessage(const string& action, const string& parameter="")
  {
    // Send out the message.
    try
    {
      string msg = action + " * HTTP/1.0\r\n" + createReplyMessage(parameter);
      BOOST_FOREACH(const socket_string_pair& pair, m_sockets)
      {
        //// Multicast.
        //pair.first->send_to(boost::asio::buffer(msg), m_notifyEndpoint);

        // Broadcast.
        boost::asio::ip::udp::endpoint broadcastEndpoint(boost::asio::ip::address::from_string(pair.second), m_port + 1);
        pair.first->send_to(boost::asio::buffer(msg), broadcastEndpoint);
      }
    }
    catch (std::exception& e)
    {
      eprintf("NetworkServiceAdvertiser: Error broadcasting message: %s", e.what());
    }
  }
  
  /// Handle incoming data.
  void handleRead(const udp_socket_ptr& socket, const boost::system::error_code& error, size_t bytes, int interfaceIndex)
  {
    const char Search[] = "M-SEARCH * ";

    if (!error)
    {
      if (m_ignoredAddresses.find(m_endpoint.address().to_string()) != m_ignoredAddresses.end())
      {
        // Ignore this packet.
        iprintf("NetworkService: Ignoring a packet from this uninteresting interface %s.", m_endpoint.address().to_string().c_str());
      }
      // We only reply if the search query at least begins with Search
      else if (memcmp(Search, m_data, sizeof(Search)-sizeof(Search[0])) == 0)
      {
        try
        {
          // Create the reply
          string reply = "HTTP/1.0 200 OK\r\n" + createReplyMessage();

          // Write the reply back to the client and wait for the next packet.
          socket->send_to(boost::asio::buffer(reply), m_endpoint);
        }
        catch (std::exception& e)
        {
          eprintf("NetworkServiceAdvertiser: Error replying to broadcast packet: %s", e.what());
        }
      }
    }
    else if (error == boost::asio::error::operation_aborted)
    {
      iprintf("Network service: socket shutdown or aborted. bye!");
      return;
    }
    else
    {
      eprintf("Network Service: Error in advertiser handle read: %d (%s) socket=%d", error.value(), error.message().c_str(), socket->native_handle());
      usleep(1000 * 100);
    }
    
    // If the socket is open, keep receiving (On XP we need to abandon a socket for 10022 - An invalid argument was supplied - as well).
    if (socket->is_open() && error != boost::asio::error::invalid_argument)
      socket->async_receive_from(boost::asio::buffer(m_data, NS_MAX_PACKET_SIZE), m_endpoint, boost::bind(&NetworkServiceAdvertiser::handleRead, this, socket, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, interfaceIndex));
    else
      iprintf("Network Service: Abandoning advertise socket, it was closed.");
  }
  
  // Turn the parameter map into HTTP headers.
  string createReplyMessage(const string& parameter="")
  {
    string reply;

    map<string, string> params;
    createReply(params);

    reply = "Content-Type: " + getType() + CRLF;
    reply += "Resource-Identifier: " + getResourceIdentifier() + CRLF;
    BOOST_FOREACH(string_pair param, params)
      reply += param.first + ": " + param.second + CRLF;

    // See if there's a body.
    string body = getBody();
    if (body.empty() == false)
      reply += "Content-Length: " + boost::lexical_cast<string>(body.size()) + CRLF;
    
    // See if there's a parameter.
    if (parameter.empty() == false)
      reply += "Parameters: " + parameter;
    
    reply += CRLF;
    reply += body;
    
    return reply;
  }
  
  unsigned short                   m_port;
  udp_socket_ptr                   m_broadcastSocket;
  vector<socket_string_pair>       m_sockets;
  std::set<std::string>            m_ignoredAddresses;
  boost::asio::ip::udp::endpoint   m_endpoint;
  boost::asio::ip::udp::endpoint   m_notifyEndpoint;
  char                             m_data[NS_MAX_PACKET_SIZE];
};
