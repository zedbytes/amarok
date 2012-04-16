/****************************************************************************************
 * Copyright (c) 2012 Matěj Laitl <matej@laitl.cz>                                      *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#include "IpodPlaylistProvider.h"

#include "IpodCollection.h"
#include "IpodPlaylist.h"
#include "core/capabilities/ActionsCapability.h"
#include "core/interfaces/Logger.h"
#include "core/support/Components.h"
#include "core/support/Debug.h"
#include "core-impl/collections/support/FileCollectionLocation.h"
#include "core-impl/meta/file/File.h"

#include <glib.h>
#include <gpod/itdb.h>


IpodPlaylistProvider::IpodPlaylistProvider( IpodCollection* collection )
    : UserPlaylistProvider( collection )
    , m_coll( collection )
{
    m_consolidateAction = new QAction( KIcon( "dialog-ok-apply" ), i18n( "Re-add orphaned and forget stale tracks" ), this );
    connect( m_consolidateAction, SIGNAL(triggered()), SLOT(slotConsolidateStaleOrphaned()) );
}

IpodPlaylistProvider::~IpodPlaylistProvider()
{
}

QString
IpodPlaylistProvider::prettyName() const
{
    return m_coll->prettyName();
}

KIcon
IpodPlaylistProvider::icon() const
{
    return m_coll->icon();
}

int
IpodPlaylistProvider::playlistCount() const
{
    return m_playlists.count();
}

Playlists::PlaylistList
IpodPlaylistProvider::playlists()
{
    return m_playlists;
}

Playlists::PlaylistPtr
IpodPlaylistProvider::addPlaylist( Playlists::PlaylistPtr playlist )
{
    return save( playlist->tracks(), playlist->name() );
}

Meta::TrackPtr
IpodPlaylistProvider::addTrack( Meta::TrackPtr track )
{
    QString name = KGlobal::locale()->formatDateTime( QDateTime::currentDateTime() );
    return save( Meta::TrackList() << track , name )->tracks().last();
}

Playlists::PlaylistPtr
IpodPlaylistProvider::save( const Meta::TrackList &tracks, const QString &name )
{
    if( !isWritable() )
        return Playlists::PlaylistPtr();

    IpodPlaylist *playlist = new IpodPlaylist( tracks, name, m_coll );
    itdb_playlist_add( m_coll->m_itdb, playlist->itdbPlaylist(), -1 );
    Playlists::PlaylistPtr playlistPtr( playlist );
    m_playlists << playlistPtr;
    subscribeTo( playlistPtr );
    emit playlistAdded( playlistPtr );
    emit startWriteDatabaseTimer();
    return playlistPtr;
}

QActionList
IpodPlaylistProvider::providerActions()
{
    QActionList actions = Playlists::UserPlaylistProvider::providerActions();
    Capabilities::ActionsCapability *ac = m_coll->create<Capabilities::ActionsCapability>();
    actions << ac->actions();
    delete ac;
    if( m_stalePlaylist || m_orphanedPlaylist )
        actions << m_consolidateAction;
    return actions;
}

QActionList
IpodPlaylistProvider::playlistActions( Playlists::PlaylistPtr playlist )
{
    QList<QAction *> actions;
    if( !m_playlists.contains( playlist ) )  // make following static cast safe
        return actions;
    KSharedPtr<IpodPlaylist> ipodPlaylist = KSharedPtr<IpodPlaylist>::staticCast( playlist );
    switch( ipodPlaylist->type() )
    {
        case IpodPlaylist::Normal:
            actions << Playlists::UserPlaylistProvider::playlistActions( playlist );
            break;
        case IpodPlaylist::Stale:
        case IpodPlaylist::Orphaned:
            actions << m_consolidateAction;
            break;
    }

    return actions;
}

QActionList
IpodPlaylistProvider::trackActions( Playlists::PlaylistPtr playlist, int trackIndex )
{
    QList<QAction *> actions;
    if( !m_playlists.contains( playlist ) )  // make following static cast safe
        return actions;
    KSharedPtr<IpodPlaylist> ipodPlaylist = KSharedPtr<IpodPlaylist>::staticCast( playlist );
    switch( ipodPlaylist->type() )
    {
        case IpodPlaylist::Normal:
            actions << Playlists::UserPlaylistProvider::trackActions( playlist, trackIndex );
            break;
        case IpodPlaylist::Stale:
        case IpodPlaylist::Orphaned:
            actions << m_consolidateAction;
            break;
    }

    return actions;
}

bool
IpodPlaylistProvider::isWritable()
{
    return m_coll->isWritable();
}

void
IpodPlaylistProvider::rename( Playlists::PlaylistPtr playlist, const QString &newName )
{
    if( !m_playlists.contains( playlist ) )  // make following static cast safe
        return;
    KSharedPtr<IpodPlaylist> ipodPlaylist = KSharedPtr<IpodPlaylist>::staticCast( playlist );
    if( ipodPlaylist->type() != IpodPlaylist::Normal )
        return;  // special playlists cannot be renamed

    playlist->setName( newName );
    emit updated();
    emit startWriteDatabaseTimer();
}

bool
IpodPlaylistProvider::deletePlaylists( Playlists::PlaylistList playlistlist )
{
    if( !isWritable() )
        return false;

    foreach( Playlists::PlaylistPtr playlist, playlistlist )
    {
        if( !m_playlists.contains( playlist ) )
            continue;
        if( KSharedPtr<IpodPlaylist>::staticCast( playlist )->type() != IpodPlaylist::Normal )
            continue;  // special playlists cannot be removed using this method
        m_playlists.removeOne( playlist );

        unsubscribeFrom( playlist );
        IpodPlaylist *ipodPlaylist = static_cast<IpodPlaylist *>( playlist.data() );
        itdb_playlist_unlink( ipodPlaylist->itdbPlaylist() );

        emit playlistRemoved( playlist );
        emit startWriteDatabaseTimer();
    }
    return true;
}

void
IpodPlaylistProvider::trackAdded( Playlists::PlaylistPtr, Meta::TrackPtr, int )
{
    emit startWriteDatabaseTimer();
}

void
IpodPlaylistProvider::trackRemoved( Playlists::PlaylistPtr, int )
{
    emit startWriteDatabaseTimer();
}

void
IpodPlaylistProvider::scheduleCopyAndInsertToPlaylist( KSharedPtr<IpodPlaylist> playlist )
{
    m_copyTracksTo.insert( playlist );
    QTimer::singleShot( 0, this, SLOT(slotCopyAndInsertToPlaylists()) );
}

void
IpodPlaylistProvider::removeTrackFromPlaylists( Meta::TrackPtr track )
{
    foreach( Playlists::PlaylistPtr playlist, m_playlists )
    {
        int trackIndex;
        // track may be multiple times in a playlist:
        while( ( trackIndex = playlist->tracks().indexOf( track ) ) >= 0 )
            playlist->removeTrack( trackIndex );
    }
}

void
IpodPlaylistProvider::parseItdbPlaylists( const Meta::TrackList &staleTracks, const QSet<QString> &knownPaths )
{
    if( !staleTracks.isEmpty() )
    {
        m_stalePlaylist = Playlists::PlaylistPtr( new IpodPlaylist( staleTracks,
            i18nc( "iPod playlist name", "Stale tracks" ), m_coll, IpodPlaylist::Stale ) );
        m_playlists << m_stalePlaylist;  // we dont subscribe to this playlist, no need to update database
        emit playlistAdded( m_stalePlaylist );
    }

    Meta::TrackList orphanedTracks = findOrphanedTracks( knownPaths );
    if( !orphanedTracks.isEmpty() )
    {
        m_orphanedPlaylist = Playlists::PlaylistPtr( new IpodPlaylist( orphanedTracks,
            i18nc( "iPod playlist name", "Orphaned tracks" ), m_coll, IpodPlaylist::Orphaned ) );
        m_playlists << m_orphanedPlaylist;  // we dont subscribe to this playlist, no need to update database
        emit playlistAdded( m_orphanedPlaylist );
    }

    if( !m_coll->m_itdb )
        return;

    for( GList *playlists = m_coll->m_itdb->playlists; playlists; playlists = playlists->next )
    {
        Itdb_Playlist *playlist = (Itdb_Playlist *) playlists->data;
        if( !playlist )
            continue;
        if( itdb_playlist_is_mpl( playlist ) )
            continue; // skip master playlist
        Playlists::PlaylistPtr playlistPtr( new IpodPlaylist( playlist, m_coll ) );
        m_playlists << playlistPtr;
        subscribeTo( playlistPtr );
        emit playlistAdded( playlistPtr );
    }

    if( m_stalePlaylist || m_orphanedPlaylist )
    {
        QString text = i18n( "Stale and/or orphaned tracks detected on %1. You can go to "
            "Saved Playlists to add orphaned tracks back to iTunes database and to remove "
            "stale entries.", m_coll->prettyName() );
        Amarok::Components::logger()->longMessage( text );
    }
}

void IpodPlaylistProvider::slotCopyAndInsertToPlaylists()
{
    QMutableSetIterator< KSharedPtr<IpodPlaylist> > it( m_copyTracksTo );
    while( it.hasNext() )
    {
        KSharedPtr<IpodPlaylist> ipodPlaylist = it.next();
        TrackPositionList tracks = ipodPlaylist->takeTracksToCopy();
        copyAndInsertToPlaylist( tracks, Playlists::PlaylistPtr::staticCast( ipodPlaylist ) );
        it.remove();
    }
}

void
IpodPlaylistProvider::slotConsolidateStaleOrphaned()
{
    int matched = 0, added = 0, removed = 0, failed = 0;

    /* Sometimes users accidentaly rename files on iPod. This creates a pair of a stale
     * iTunes database entry and an orphaned file. Find these specifically and move the files
     * back to their original location. */
    if( m_stalePlaylist && m_orphanedPlaylist )
    {
        QMap<Meta::TrackPtr, Meta::TrackPtr> orphanedToStale;
        foreach( Meta::TrackPtr orphanedTrack, m_orphanedPlaylist->tracks() )
        {
            Meta::TrackPtr matchingStaleTrack;
            foreach( Meta::TrackPtr staleTrack, m_stalePlaylist->tracks() )
            {
                if( orphanedAndStaleTracksMatch( orphanedTrack, staleTrack ) )
                {
                    matchingStaleTrack = staleTrack;
                    break;
                }
            }

            if( matchingStaleTrack )  // matching track found
            {
                orphanedToStale.insert( orphanedTrack, matchingStaleTrack );
                m_stalePlaylist->removeTrack( m_stalePlaylist->tracks().indexOf( matchingStaleTrack ) );
            }
        }

        QMapIterator<Meta::TrackPtr, Meta::TrackPtr> it( orphanedToStale );
        while( it.hasNext() )
        {
            it.next();
            Meta::TrackPtr orphanedTrack = it.key();
            Meta::TrackPtr staleTrack = it.value();
            m_orphanedPlaylist->removeTrack( m_orphanedPlaylist->tracks().indexOf( orphanedTrack ) );

            QString from = orphanedTrack->playableUrl().toLocalFile();
            QString to = staleTrack->playableUrl().toLocalFile();
            if( !QFileInfo( to ).absoluteDir().mkpath( "." ) )
            {
                warning() << __PRETTY_FUNCTION__ << "Failed to create directory path"
                          << QFileInfo( to ).absoluteDir().path();
                failed++;
                continue;
            }
            if( !QFile::rename( from, to ) )
            {
                warning() << __PRETTY_FUNCTION__ << "Failed to move track from" << from
                          << "to" << to;
                failed++;
                continue;
            }
            matched++;
        }
    }

    // remove remaining stale tracks
    if( m_stalePlaylist && m_stalePlaylist->trackCount() )
    {
        Collections::CollectionLocation *location = m_coll->location();
        // hide removal confirmation - these tracks are already deleted, don't confuse user
        static_cast<IpodCollectionLocation *>( location )->setHidingRemoveConfirm( true );
        removed += m_stalePlaylist->trackCount();
        location->prepareRemove( m_stalePlaylist->tracks() );
        // remove all tracks from the playlist, assume the removal suceeded
        while( m_stalePlaylist->trackCount() )
            m_stalePlaylist->removeTrack( 0 );
    }

    // add remaining orphaned tracks back to database
    if( m_orphanedPlaylist && m_orphanedPlaylist->trackCount() )
    {
        Collections::CollectionLocation *src = new Collections::FileCollectionLocation();
        Collections::CollectionLocation *dest = m_coll->location();
        added += m_orphanedPlaylist->trackCount();
        src->prepareMove( m_orphanedPlaylist->tracks(), dest );
        // remove all tracks from the playlist, assume the move suceeded
        while( m_orphanedPlaylist->trackCount() )
            m_orphanedPlaylist->removeTrack( 0 );
    }

    // if some of the playlists became empty, remove them completely. no need to
    // unsubscribe - we were not subscribed
    if( m_stalePlaylist && m_stalePlaylist->trackCount() == 0 )
    {
        m_playlists.removeOne( m_stalePlaylist );
        emit playlistRemoved( m_stalePlaylist );
        m_stalePlaylist = 0;
    }
    if( m_orphanedPlaylist && m_orphanedPlaylist->trackCount() == 0 )
    {
        m_playlists.removeOne( m_orphanedPlaylist );
        emit playlistRemoved( m_orphanedPlaylist );
        m_orphanedPlaylist = 0;
    }

    QString failedText = failed ? i18np("Failed to process one track. (more info about "
        "it is in the Amarok debugging log)", "Failed to process %4 tracks. (more info "
        "about these is in the Amarok debugging log)", failed ) : QString();
    QString text = i18nc( "Infrequently displayed message, don't bother with singlar "
        "forms. %1 to %3 are numbers, %4 is the 'Failed to process ...' sentence or an "
        "empty string.", "Done consolidating iPod files. %1 orphaned tracks matched with "
        "stale iTunes database entries, %2 stale database entries removed, %3 orphaned "
        "tracks added back to the iTunes database. %4", matched, removed, added,
        failedText );
    Amarok::Components::logger()->longMessage( text );
}

void IpodPlaylistProvider::copyAndInsertToPlaylist( const TrackPositionList &tracks, Playlists::PlaylistPtr destPlaylist )
{
    QMap<Collections::Collection*, TrackPositionList> sourceCollections;
    foreach( TrackPosition pair, tracks )
    {
        Collections::Collection *coll = pair.first->collection();
        if( coll == m_coll )
            continue;

        if( sourceCollections.contains( coll ) )
            sourceCollections[ coll ] << pair;
        else
            sourceCollections.insert( coll, TrackPositionList() << pair );
    }

    foreach( Collections::Collection *coll, sourceCollections.keys() )
    {
        Meta::TrackList sourceTracks;
        QMap<Meta::TrackPtr, int> trackPlaylistPositions;
        foreach( TrackPosition pair, sourceCollections.value( coll ) )
        {
            sourceTracks << pair.first;
            trackPlaylistPositions.insert( pair.first, pair.second );
        }

        Collections::CollectionLocation *sourceLocation = coll
            ? coll->location() : new Collections::FileCollectionLocation();
        Q_ASSERT( sourceLocation );
        // prepareCopy() takes ownership of the pointers, we must create target collection every time
        IpodCollectionLocation *targetLocation = static_cast<IpodCollectionLocation *>( m_coll->location() );

        targetLocation->setDestinationPlaylist( destPlaylist, trackPlaylistPositions );
        sourceLocation->prepareCopy( sourceTracks, targetLocation );
    }
}

Meta::TrackList
IpodPlaylistProvider::findOrphanedTracks( const QSet<QString> &knownPaths )
{
    gchar *musicDirChar = itdb_get_music_dir( QFile::encodeName( m_coll->mountPoint() ) );
    QString musicDirPath = QFile::decodeName( musicDirChar );
    g_free( musicDirChar );
    musicDirChar = 0;

    QStringList trackPatterns;
    foreach( QString suffix, m_coll->supportedFormats() )
    {
        trackPatterns << QString( "*.%1" ).arg( suffix );
    }

    Meta::TrackList orphanedTracks;
    QDir musicDir( musicDirPath );
    foreach( QString subdir, musicDir.entryList( QStringList( "F??" ), QDir::Dirs | QDir::NoDotAndDotDot ) )
    {
        subdir = musicDir.absoluteFilePath( subdir ); // make the path absolute
        foreach( QFileInfo info, QDir( subdir ).entryInfoList( trackPatterns ) )
        {
            QString canonPath = info.canonicalFilePath();
            if( knownPaths.contains( canonPath ) )
                continue;  // already in iTunes database
            Meta::TrackPtr track( new MetaFile::Track( KUrl( canonPath ) ) );
            orphanedTracks << track;
        }
    }
    return orphanedTracks;
}

bool
IpodPlaylistProvider::orphanedAndStaleTracksMatch( const Meta::TrackPtr &orphaned, const Meta::TrackPtr &stale )
{
    if( orphaned->filesize() != stale->filesize() )  // first for performance reasons
        return false;
    if( orphaned->length() != stale->length() )
        return false;
    if( orphaned->name() != stale->name() )
        return false;
    if( orphaned->type() != stale->type() )
        return false;
    if( orphaned->trackNumber() != stale->trackNumber() )
        return false;
    if( orphaned->discNumber() != stale->discNumber() )
        return false;

    if( entitiesDiffer( orphaned->album(), stale->album() ) )
        return false;
    if( entitiesDiffer( orphaned->artist(), stale->artist() ) )
        return false;
    if( entitiesDiffer( orphaned->composer(), stale->composer() ) )
        return false;
    if( entitiesDiffer( orphaned->genre(), stale->genre() ) )
        return false;
    if( entitiesDiffer( orphaned->year(), stale->year() ) )
        return false;

    return true;
}

template <class T> bool
IpodPlaylistProvider::entitiesDiffer( T first, T second )
{
    return ( first ? first->name() : QString() ) != ( second ? second->name() : QString() );
}

#include "IpodPlaylistProvider.moc"
