#include "PlexDirectoryTypeParserDevice.h"
#include "PlexDirectory.h"

/////////////////////////////////////////////////////////////////////////////////////////
void CPlexDirectoryTypeParserDevice::Process(CFileItem& item, CFileItem& mediaContainer, XML_ELEMENT* itemElement)
{
  for (XML_ELEMENT* connection = itemElement->first_node(); connection; connection = connection->next_sibling())
  {
    CFileItemPtr connItem = XFILE::CPlexDirectory::NewPlexElement(connection, item, item.GetPath());
    item.m_connections.push_back(connItem);
  }
}

