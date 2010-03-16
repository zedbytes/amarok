/****************************************************************************************
 * Copyright (c) 2008 Nikolaj Hald Nielsen <nhn@kde.org>                                *
 * Copyright (c) 2008 Soren Harward <stharward@gmail.com>                               *
 * Copyright (c) 2009 Téo Mrnjavac <teo.mrnjavac@gmail.com>                             *
 * Copyright (c) 2010 Nanno Langstraat <langstr@gmail.com>                              *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) version 3 or        *
 * any later version accepted by the membership of KDE e.V. (or its successor approved  *
 * by the membership of KDE e.V.), which shall act as a proxy defined in Section 14 of  *
 * version 3 of the license.                                                            *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#define DEBUG_PREFIX "Playlist::AlbumNavigator"

#include "AlbumNavigator.h"

#include "playlist/PlaylistModelStack.h"

#include "Debug.h"
#include "Meta.h"

#include <QtAlgorithms> // For qStableSort


void
Playlist::AlbumNavigator::notifyItemsInserted( const QSet<quint64> &insertedItems )
{
    DEBUG_BLOCK

    QSet<Meta::AlbumPtr> oldAlbums = m_itemsPerAlbum.uniqueKeys().toSet();
    QSet<Meta::AlbumPtr> modifiedAlbums;

    foreach( quint64 insertedItem, insertedItems )
    {
        Meta::AlbumPtr album = m_model->trackForId( insertedItem )->album();
        debug() << "Inserting" << album->prettyName();
        m_itemsPerAlbum[album].append( insertedItem ); // conveniently creates an empty list if none exists
        modifiedAlbums.insert( album );
    }

        foreach ( Meta::AlbumPtr a, m_itemsPerAlbum.uniqueKeys() )
        {
            debug() << "Album:" << a->prettyName();
        }
    foreach( Meta::AlbumPtr album, modifiedAlbums )
        qStableSort( m_itemsPerAlbum[album].begin(), m_itemsPerAlbum[album].end(), itemLessThan );

    notifyAlbumsInserted( ( modifiedAlbums - oldAlbums ).toList() );
}

void
Playlist::AlbumNavigator::notifyItemsRemoved( const QSet<quint64> &removedItems )
{
    DEBUG_BLOCK

    foreach( quint64 removedItem, removedItems )
    {
        Meta::AlbumPtr album = m_model->trackForId( removedItem )->album();

        // Try not to lose our position in the playlist: if we're losing 'currentItem()', substitute the next "planned item".
        if ( removedItem == currentItem() )
        {
            planOne();    // Could select 'removedItem' again; in that case our parent will 'setCurrentItem( 0 )'.
            if ( !m_plannedItems.isEmpty() )
                setCurrentItem( m_plannedItems.first() );
        }

        m_plannedItems.removeAll( removedItem );    // We only need to do this because we call 'planOne()' in this loop.

        // Maintain 'm_itemsPerAlbum'.
        ItemList itemsInAlbum = m_itemsPerAlbum.value( album );
        itemsInAlbum.removeAll( removedItem );
        if ( itemsInAlbum.isEmpty() )
        {
            m_itemsPerAlbum.remove( album );
            m_plannedAlbums.removeAll( album );
        }
    }
}

bool
Playlist::AlbumNavigator::itemLessThan( const quint64 &item1, const quint64 &item2 )
{
    // Somewhat nasty to hard-code the model like this, but 'qStableSort()' doesn't allow us pass 'm_model'.
    AbstractModel *model = Playlist::ModelStack::instance()->top();

    Meta::TrackPtr track1 = model->trackForId( item1 );
    Meta::TrackPtr track2 = model->trackForId( item2 );

    return Meta::Track::lessThan( track1, track2 );
}
