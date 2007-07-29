/***************************************************************************
 * copyright            : (C) 2007 Ian Monroe <ian@monroe.nu>              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License version 2        *
 *   as published by the Free Software Foundation.                         *
 ***************************************************************************/

#ifndef AMAROKPLAYLISTITEM_H
#define AMAROKPLAYLISTITEM_H

#include "meta.h"

#include <QMetaType>

class QFontMetricsF;
class QGraphicsScene;

namespace PlaylistNS {

    class ItemScene;

    class Item
    {
        public:
            Item() : m_scene( 0 ) { }
            Item( Meta::TrackPtr track );
            ~Item();
            Meta::TrackPtr track() const { return m_track; }
            QGraphicsScene* scene( int totalWidth = -1 );

        private:
            Meta::TrackPtr m_track;
            ItemScene* m_scene;
    };

}

Q_DECLARE_METATYPE( PlaylistNS::Item* )

#endif
