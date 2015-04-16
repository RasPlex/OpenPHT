#ifndef _PLEX_HOME_THEATER_PLEXDIRECTORYTYPEPARSERDEVICE_H_
#define _PLEX_HOME_THEATER_PLEXDIRECTORYTYPEPARSERDEVICE_H_

#include "PlexDirectoryTypeParser.h"

class CPlexDirectoryTypeParserDevice : public CPlexDirectoryTypeParserBase
{
public:
  virtual void Process(CFileItem& item, CFileItem& mediaContainer, XML_ELEMENT* itemElement);
};


#endif //_PLEX_HOME_THEATER_PLEXDIRECTORYTYPEPARSERDEVICE_H_
