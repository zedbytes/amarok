/***************************************************************************
 * copyright            : (c) 2004 Pierpaolo Di Panfilo                    *
 *                        (c) 2004 Mark Kretschmann <markey@web.de>        *
 *                        (c) 2005-2006 Seb Ruiz <me@sebruiz.net>          *
 *                        (c) 2005 Christian Muehlhaeuser <chris@chris.de> *
 *                        (c) 2006 Bart Cerneels <bart.cerneels@gmail.com> *
 *                        (c) 2006 Ian Monroe <ian@monroe.nu>              *
 *                        (c) 2006 Alexandre Oliveira <aleprj@gmail.com>   *
 *                        (c) 2006 Adam Pigg <adam@piggz.co.uk>            *
 *                        (c) 2006 Bonne Eggleston <b.eggleston@gmail.com> *
 * See COPYING file for licensing information                              *
 ***************************************************************************/

#include "playlistbrowseritem.h"

#include "amarok.h"
#include "debug.h"
#include "dynamicmode.h"
#include "k3bexporter.h"
#include "mediabrowser.h"
#include "metabundle.h"
#include "playlistbrowser.h"
#include "playlist.h"
#include "playlistloader.h"    //load()
#include "playlist/PlaylistModel.h"
#include "playlistselection.h"
#include "progressBar.h"
#include "querybuilder.h"
#include "statusbar.h"
#include "tagdialog.h"
#include "TheInstances.h"
#include "threadmanager.h"

#include <KApplication>      //Used for Shoutcast random name generation
#include <KIconLoader>       //smallIcon
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KJobUiDelegate>
#include <KLocale>
#include <KMenu>
#include <KMessageBox>       //podcast info box
#include <KMimeType>
#include <KMimeTypeTrader>
#include <KRandom>
#include <KRun>
#include <KStandardDirs>     //podcast loading icons
#include <KUrlRequester>
#include <KVBox>

#include <Q3PopupMenu>
#include <QDateTime>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>          //paintCell()
#include <QPixmap>           //paintCell()
#include <QRegExp>


/////////////////////////////////////////////////////////////////////////////
///    CLASS PlaylistReader
////////////////////////////////////////////////////////////////////////////

class PlaylistReader : public ThreadManager::DependentJob
{
    public:
        PlaylistReader( QObject *recipient, const QString &path )
                : ThreadManager::DependentJob( recipient, "PlaylistReader" )
                , m_path( path ) {}

        virtual bool doJob() {
            DEBUG_BLOCK
            PlaylistFile pf = PlaylistFile( m_path );
            title = pf.title();
            for( BundleList::iterator it = pf.bundles().begin();
                    it != pf.bundles().end();
                    ++it )
                bundles += MetaBundle( (*it).url() );
            return true;
        }

        virtual void completeJob() {
            DEBUG_BLOCK
            PlaylistFile pf = PlaylistFile( m_path );
            ThreadManager::DependentJob::completeJob();
        }

        BundleList bundles;
        QString    title;

    private:
        const QString m_path;
};

/////////////////////////////////////////////////////////////////////////////
///    CLASS PlaylistBrowserEntry
////////////////////////////////////////////////////////////////////////////

int
PlaylistBrowserEntry::compare( Q3ListViewItem* item, int col, bool ascending ) const
{
    bool i1 = rtti() == PlaylistCategory::RTTI;
    bool i2 = item->rtti() == PlaylistCategory::RTTI;

    // If only one of them is a category, make it show up before
    if ( i1 != i2 )
        return i1 ? -1 : 1;
    else if ( i1 ) //both are categories
    {
        PlaylistBrowser * const pb = PlaylistBrowser::instance();

        Q3ValueList<PlaylistCategory*> toplevels; //define a static order for the toplevel categories
        toplevels << pb->m_playlistCategory
                  << pb->m_smartCategory
                  << pb->m_dynamicCategory
                  << pb->m_streamsCategory
                  << pb->m_podcastCategory;

        for( int i = 0, n = toplevels.count(); i < n; ++i )
        {
            if( this == toplevels[i] )
                return ascending ? -1 : 1; //same order whether or not it's ascending
            if( item == toplevels[i] )
                return ascending ? 1 : -1;
        }
    }

    return K3ListViewItem::compare(item, col, ascending);
}

void
PlaylistBrowserEntry::setKept( bool k )
{
    m_kept = k;
    if ( !k )   //Disable renaming by two single clicks
        setRenameEnabled( 0, false );
}

void
PlaylistBrowserEntry::updateInfo()
{
    PlaylistBrowser::instance()->setInfo( QString(), QString() );
    return;
}

void
PlaylistBrowserEntry::slotDoubleClicked()
{
    warning() << "No functionality for item double click implemented" << endl;
}

void
PlaylistBrowserEntry::slotRenameItem()
{
    Q3ListViewItem *parent = K3ListViewItem::parent();

    while( parent )
    {
        if( !static_cast<PlaylistBrowserEntry*>( parent )->isKept() )
            return;
        if( !parent->parent() )
            break;
        parent = parent->parent();
    }

    setRenameEnabled( 0, true );
    static_cast<PlaylistBrowserView*>( listView() )->rename( this, 0 );
}

void
PlaylistBrowserEntry::slotPostRenameItem( const QString /*newName*/ )
{
    setRenameEnabled( 0, false );
}

/////////////////////////////////////////////////////////////////////////////
///    CLASS PlaylistCategory
////////////////////////////////////////////////////////////////////////////

PlaylistCategory::PlaylistCategory( Q3ListView *parent, Q3ListViewItem *after, const QString &t, bool isFolder )
    : PlaylistBrowserEntry( parent, after )
    , m_title( t )
    , m_id( -1 )
    , m_folder( isFolder )
{
    setDragEnabled( false );
    setRenameEnabled( 0, isFolder );
    setPixmap( 0, SmallIcon( Amarok::icon( "files2" ) ) );
    setText( 0, t );
}


PlaylistCategory::PlaylistCategory( PlaylistCategory *parent, Q3ListViewItem *after, const QString &t, bool isFolder )
    : PlaylistBrowserEntry( parent, after )
    , m_title( t )
    , m_id( -1 )
    , m_folder( isFolder )
{
    setDragEnabled( false );
    setRenameEnabled( 0, isFolder );
    setPixmap( 0, SmallIcon( Amarok::icon( "files" ) ) );
    setText( 0, t );
}


PlaylistCategory::PlaylistCategory( Q3ListView *parent, Q3ListViewItem *after, const QDomElement &xmlDefinition, bool isFolder )
    : PlaylistBrowserEntry( parent, after )
    , m_id( -1 )
    , m_folder( isFolder )
{
    setXml( xmlDefinition );
    setDragEnabled( false );
    setRenameEnabled( 0, isFolder );
    setPixmap( 0, SmallIcon( Amarok::icon( "files2") ) );
}


PlaylistCategory::PlaylistCategory( PlaylistCategory *parent, Q3ListViewItem *after, const QDomElement &xmlDefinition )
    : PlaylistBrowserEntry( parent, after )
    , m_id( -1 )
    , m_folder( true )
{
    setXml( xmlDefinition );
    setDragEnabled( false );
    setRenameEnabled( 0, true );
    setPixmap( 0, SmallIcon( Amarok::icon( "files" ) ) );
}

PlaylistCategory::PlaylistCategory( PlaylistCategory *parent, Q3ListViewItem *after, const QString &t, const int id )
    : PlaylistBrowserEntry( parent, after )
    , m_title( t )
    , m_id( id )
    , m_folder( true )
{
    setDragEnabled( false );
    setRenameEnabled( 0, true );
    setPixmap( 0, SmallIcon( Amarok::icon( "files" ) ) );
    setText( 0, t );
}

void PlaylistCategory::okRename( int col )
{
    Q3ListViewItem::okRename( col );

    if( m_id < 0 )  return;

    // update the database entry to have the correct name
    const int parentId = parent() ? static_cast<PlaylistCategory*>(parent())->id() : 0;
    CollectionDB::instance()->updatePodcastFolder( m_id, text(0), parentId, isOpen() );
}

void PlaylistCategory::setXml( const QDomElement &xml )
{
    PlaylistBrowser *pb = PlaylistBrowser::instance();
    QString tname = xml.tagName();
    if ( tname == "category" )
    {
        setOpen( xml.attribute( "isOpen" ) == "true" );
        m_title = xml.attribute( "name" );
        setText( 0, m_title );
        Q3ListViewItem *last = 0;
        for( QDomNode n = xml.firstChild() ; !n.isNull(); n = n.nextSibling() )
        {
            QDomElement e = n.toElement();
            if ( e.tagName() == "category" )
                last = new PlaylistCategory( this, last, e);

            else if ( e.tagName() == "default" ) {
                if( e.attribute( "type" ) == "stream" )
                    pb->m_coolStreamsOpen   = (e.attribute( "isOpen" ) == "true");
                if( e.attribute( "type" ) == "smartplaylist" )
                    pb->m_smartDefaultsOpen = (e.attribute( "isOpen" ) == "true");
                if( e.attribute( "type" ) == "lastfm" )
                    pb->m_lastfmOpen = (e.attribute( "isOpen" ) == "true");
                continue;
            }
            else if ( e.tagName() == "stream" )
                last = new StreamEntry( this, last, e );

            else if ( e.tagName() == "smartplaylist" )
                last = new SmartPlaylist( this, last, e );

            else if ( e.tagName() == "playlist" )
                last = new PlaylistEntry( this, last, e );

            else if ( e.tagName() == "lastfm" )
                last = new LastFmEntry( this, last, e );

            else if ( e.tagName() == "dynamic" ) {
                if ( e.attribute( "name" ) == i18n("Random Mix") || e.attribute( "name" ) == i18n("Suggested Songs" ) )
                    continue;
                last = new DynamicEntry( this, last, e );
            }
            else if ( e.tagName() == "podcast" )
            {
                const KUrl url( n.namedItem( "url").toElement().text() );
                QString xmlLocation = Amarok::saveLocation( "podcasts/" );
                xmlLocation        += n.namedItem( "cache" ).toElement().text();

                QDomDocument xml;
                QFile xmlFile( xmlLocation );
                QTextStream stream( &xmlFile );
                stream.setCodec( QTextCodec::codecForName( "UTF-8" ) );
                stream.setAutoDetectUnicode( true );

                if( !xmlFile.open( QIODevice::ReadOnly ) || !xml.setContent( stream.read() ) )
                {
                    // Invalid podcasts should still be added to the browser, which means there is no cached xml.
                    last = new PodcastChannel( this, last, url, n );
                }
                else
                    last = new PodcastChannel( this, last, url, n, xml );

                #define item static_cast<PodcastChannel*>(last)
                if( item->autoscan() )
                    pb->m_podcastItemsToScan.append( item );
                #undef  item
            }
            else if ( e.tagName() == "settings" )
                PlaylistBrowser::instance()->registerPodcastSettings(  title(), new PodcastSettings( e, title() ) );

            if( !e.attribute( "isOpen" ).isNull() && last )
                last->setOpen( e.attribute( "isOpen" ) == "true" ); //settings doesn't have an attribute "isOpen"
        }
        setText( 0, xml.attribute("name") );
    }
}


QDomElement PlaylistCategory::xml() const
{
        QDomDocument d;
        QDomElement i = d.createElement("category");
        i.setAttribute( "name", text(0) );
        if( isOpen() )
            i.setAttribute( "isOpen", "true" );
        for( PlaylistBrowserEntry *it = static_cast<PlaylistBrowserEntry*>( firstChild() ); it;
             it = static_cast<PlaylistBrowserEntry*>( it->nextSibling() ) )
        {
            if( it == PlaylistBrowser::instance()->m_coolStreams )
            {
                QDomDocument doc;
                QDomElement e = doc.createElement("default");
                e.setAttribute( "type", "stream" );
                if( it->isOpen() )
                    e.setAttribute( "isOpen", "true" );
                i.appendChild( d.importNode( e, true ) );
            }
            else if( it == PlaylistBrowser::instance()->m_lastfmCategory )
            {
                QDomDocument doc;
                QDomElement e = doc.createElement("default");
                e.setAttribute( "type", "lastfm" );
                if( it->isOpen() )
                    e.setAttribute( "isOpen", "true" );
                i.appendChild( d.importNode( e, true ) );
            }
            else if( it == PlaylistBrowser::instance()->m_smartDefaults )
            {
                QDomDocument doc;
                QDomElement e = doc.createElement("default");
                e.setAttribute( "type", "smartplaylist" );
                if( it->isOpen() )
                    e.setAttribute( "isOpen", "true" );
                i.appendChild( d.importNode( e, true ) );
            }
            else if( it->isKept() )
                i.appendChild( d.importNode( it->xml(), true ) );
        }
        return i;
}

void
PlaylistCategory::slotDoubleClicked()
{
    setOpen( !isOpen() );
}

void
PlaylistCategory::slotRenameItem()
{
    if ( isKept() ) {
        setRenameEnabled( 0, true );
        static_cast<PlaylistBrowserView*>( listView() )->rename( this, 0 );
    }
}


void
PlaylistCategory::showContextMenu( const QPoint &position )
{
    KMenu menu( listView() );

    if( !isKept() ) return;

    enum Actions { RENAME, REMOVE, CREATE, PLAYLIST, PLAYLIST_IMPORT, SMART, STREAM, DYNAMIC,
                    LASTFM, LASTFMCUSTOM, PODCAST, REFRESH, CONFIG, INTERVAL };

    Q3ListViewItem *parentCat = this;

    while( parentCat->parent() )
        parentCat = parentCat->parent();


    #define connectDirect( act, theSlot ) \
      connect( act, SIGNAL( triggered() ), PlaylistBrowser::instance(), SLOT( theSlot ) );
    if( isFolder() ) {
        QAction* renameAction = new QAction( KIcon( Amarok::icon("edit") ), i18n( "&Rename" ), &menu );
        connectDirect( renameAction, renameSelectedItem() );
        QAction* deleteAction = new QAction( KIcon( Amarok::icon("remove") ), i18n( "&Delete" ), &menu );
        connectDirect( deleteAction, removeSelectedItems() );

        menu.addAction( renameAction );
        menu.addAction( deleteAction );
        menu.addSeparator();
    }
    #undef connectDirect
    class MenuActions : public QObject
    {
        public:
            MenuActions( PlaylistCategory* parent )
                : QObject( parent )
                , isPodcastFolder( false ) { }
            bool isPodcastFolder;
        public slots:
            #define parentCat qobject_cast<PlaylistCategory*>( parent() )
            void createPlaylist() { PlaylistBrowser::instance()-> createPlaylist( parentCat, false ); }
            void playlistImport() { PlaylistBrowser::instance()->openPlaylist( parentCat ); }
            void newSmartPlaylist() { PlaylistBrowser::instance()->addSmartPlaylist( parentCat ); }
            void newDynamic() { ConfigDynamic::dynamicDialog( PlaylistBrowser::instance() ); }
            void newLastFm() { PlaylistBrowser::instance()->addLastFmRadio( parentCat ); }
            void newLastFmCustom() { PlaylistBrowser::instance()->addLastFmCustomRadio( parentCat ); }
            void newStream() { PlaylistBrowser::instance()->addStream( parentCat ); }
            void newPodcast() { PlaylistBrowser::instance()->addPodcast( parentCat ); }
            void refreshPodcasts() { PlaylistBrowser::instance()->refreshPodcasts( parentCat ); }
            void configurePodcasts() { PlaylistBrowser::instance()->configurePodcasts( parentCat ); }
            void start() { PlaylistBrowser::instance()->changePodcastInterval(); }
            void addSubfolder()
            {
                Q3ListViewItem *tracker = parentCat->firstChild();
                PlaylistCategory *newFolder = 0;
                int c;
                QString name;

                for( c = 0 ; isCategory( tracker ); tracker = tracker->nextSibling() )
                {
                    if( tracker->text(0).startsWith( i18n( "Folder" ) ) )
                        c++;
                    if( !isCategory( tracker->nextSibling() ) )
                        break;
                }
                name = i18n( "Folder" );
                if( c ) name = i18n( "Folder %1", c );
                if( tracker == parentCat->firstChild() && !isCategory( tracker ) ) tracker = 0;

                newFolder = new PlaylistCategory( parentCat, tracker, name, true );
                newFolder->startRename( 0 );
                if( isPodcastFolder )
                {
                    c = CollectionDB::instance()->addPodcastFolder( newFolder->text(0), parentCat->id(), false );
                    newFolder->setId( c );
                }
            }
            #undef parentCat
    };
    MenuActions ma(this);

    if( parentCat == static_cast<Q3ListViewItem*>( PlaylistBrowser::instance()->m_playlistCategory) )
    {

        menu.addAction( KIcon(Amarok::icon( "add_playlist" )), i18n("Create Playlist..."), &ma, SLOT( createPlaylist() ) );
        menu.addAction( KIcon(Amarok::icon( "add_playlist" )), i18n("Import Playlist..."), &ma, SLOT( playlistImport() ) );
    }

    else if( parentCat == static_cast<Q3ListViewItem*>(PlaylistBrowser::instance()->m_smartCategory) )
        menu.addAction( KIcon(Amarok::icon( "add_playlist" )), i18n("New Smart Playlist..."), &ma, SLOT( newSmartPlaylist() ) );

    else if( parentCat == static_cast<Q3ListViewItem*>(PlaylistBrowser::instance()->m_dynamicCategory) )
        menu.addAction( KIcon(Amarok::icon( "add_playlist" )), i18n("New Dynamic Playlist..."), &ma, SLOT( newDynamic() ) );

    else if( parentCat == static_cast<Q3ListViewItem*>(PlaylistBrowser::instance()->m_streamsCategory) )
        menu.addAction( KIcon(Amarok::icon( "add_playlist" )), i18n("Add Radio Stream..."), &ma, SLOT( newStream() ) );

    else if( parentCat == static_cast<Q3ListViewItem*>(PlaylistBrowser::instance()->m_lastfmCategory) )
    {
        menu.addAction( KIcon(Amarok::icon( "add_playlist" )), i18n("Add Last.fm Radio..."), &ma, SLOT( newLastFm() ) );
        menu.addAction( KIcon(Amarok::icon( "add_playlist" )), i18n("Add Custom Last.fm Radio..."), &ma, SLOT( newLastFmCustom() ) );
    }

    else if( parentCat == static_cast<Q3ListViewItem*>(PlaylistBrowser::instance()->m_podcastCategory) )
    {
        ma.isPodcastFolder = true;
        menu.addAction( KIcon( Amarok::icon( "add_playlist" ) ), i18n("Add Podcast..."), &ma, SLOT( newPodcast() ) );
        menu.addAction( KIcon( Amarok::icon( "refresh" ) ), i18n("Refresh All Podcasts"), &ma, SLOT( refreshPodcasts() ) );
        menu.addSeparator();
        menu.addAction( KIcon( Amarok::icon( "configure" ) ), i18n( "&Configure Podcasts..." ), &ma, SLOT( configurePodcasts ) );
        if( parentCat == this )
            menu.addAction( KIcon( Amarok::icon( "configure" ) ), i18n("Scan Interval..."), &ma, SLOT( start() ) );
    }

    menu.addSeparator();
    menu.addAction( KIcon( Amarok::icon( "files" ) ), i18n("Create Sub-Folder"), &ma, SLOT( addSubfolder() ) );

    menu.exec( position );

}


void
PlaylistCategory::paintCell( QPainter *p, const QColorGroup &cg, int column, int width, int align )
{
    QFont font( p->font() );

    if( !m_folder ) {
        font.setBold( true );
    }

    p->setFont( font );

    K3ListViewItem::paintCell( p, cg, column, width, align );
}


/////////////////////////////////////////////////////////////////////////////
///    CLASS PlaylistEntry
////////////////////////////////////////////////////////////////////////////

PlaylistEntry::PlaylistEntry( Q3ListViewItem *parent, Q3ListViewItem *after, const KUrl &url, int tracks, int length )
    : PlaylistBrowserEntry( parent, after )
    , m_url( url )
    , m_length( length )
    , m_trackCount( tracks )
    , m_loading( false )
    , m_loaded( false )
    , m_dynamic( false )
    , m_loading1( new QPixmap( KStandardDirs::locate("data", "amarok/images/loading1.png" ) ) )
    , m_loading2( new QPixmap( KStandardDirs::locate("data", "amarok/images/loading2.png" ) ) )
    , m_lastTrack( 0 )
{
    m_trackList.setAutoDelete( true );
    tmp_droppedTracks.setAutoDelete( false );

    setDragEnabled( true );
    setRenameEnabled( 0, false );
    setExpandable( true );

    setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );

    if( !m_trackCount )
    {
        setText(0, i18n("Loading Playlist") );
        load();   //load the playlist file
    }
    // set text is called from within customEvent()
}


PlaylistEntry::PlaylistEntry( Q3ListViewItem *parent, Q3ListViewItem *after, const QDomElement &xmlDefinition )
    : PlaylistBrowserEntry( parent, after )
    , m_loading( false )
    , m_loaded( false )
    , m_dynamic( false )
    , m_loading1( new QPixmap( KStandardDirs::locate("data", "amarok/images/loading1.png" ) ) )
    , m_loading2( new QPixmap( KStandardDirs::locate("data", "amarok/images/loading2.png" ) ) )
    , m_lastTrack( 0 )
{
    m_url.setPath( xmlDefinition.attribute( "file" ) );
    m_trackCount = xmlDefinition.namedItem( "tracks" ).toElement().text().toInt();
    m_length = xmlDefinition.namedItem( "length" ).toElement().text().toInt();

    QString title = xmlDefinition.attribute( "title" );
    if( title.isEmpty() )
    {
        title = fileBaseName( m_url.path() );
        title.replace( '_', ' ' );
    }
    setText( 0, title );

    m_trackList.setAutoDelete( true );
    tmp_droppedTracks.setAutoDelete( false );

    setDragEnabled( true );
    setRenameEnabled( 0, false );
    setExpandable( true );

    setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );

    if( !m_trackCount )
    {
        setText(0, i18n("Loading Playlist") );
        load();   //load the playlist file
    }
    // set text is called from within customEvent()
}


PlaylistEntry::~PlaylistEntry()
{
    m_trackList.clear();
    tmp_droppedTracks.setAutoDelete( true );
    tmp_droppedTracks.clear();
}

void PlaylistEntry::load()
{
    if( m_loading )  return;
    m_trackList.clear();
    m_length = 0;
    m_loaded = false;
    m_loading = true;

    //starts loading animation
    m_iconCounter = 1;
    startAnimation();
    connect( &m_animationTimer, SIGNAL(timeout()), this, SLOT(slotAnimation()) );

    //delete all children, so that we don't duplicate things
    while( firstChild() )
        delete firstChild();

     //read the playlist file in a thread
    ThreadManager::instance()->queueJob( new PlaylistReader( this, m_url.path() ) );
}

void PlaylistEntry::startAnimation()
{
    if( !m_animationTimer.isActive() )
        m_animationTimer.start( ANIMATION_INTERVAL );
}

void PlaylistEntry::stopAnimation()
{
    m_animationTimer.stop();
    m_dynamic ?
        setPixmap( 0, SmallIcon( Amarok::icon( "favorites" ) ) ):
        setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );
}

void PlaylistEntry::slotAnimation()
{
    m_iconCounter % 2 ?
        setPixmap( 0, *m_loading1 ):
        setPixmap( 0, *m_loading2 );

    m_iconCounter++;
}

void PlaylistEntry::insertTracks( Q3ListViewItem *after, KUrl::List list )
{
    Q3ValueList<MetaBundle> bundles;

    oldForeachType( KUrl::List, list )
        bundles += MetaBundle( *it );

    insertTracks( after, bundles );
}

void PlaylistEntry::insertTracks( Q3ListViewItem *after, Q3ValueList<MetaBundle> bundles )
{
    int pos = 0;
    if( after ) {
        pos = m_trackList.find( static_cast<PlaylistTrackItem*>(after)->trackInfo() ) + 1;
        if( pos == -1 )
            return;
    }

    uint k = 0;
    oldForeachType( Q3ValueList<MetaBundle>, bundles )
    {
        TrackItemInfo *newInfo = new TrackItemInfo( *it );
        m_length += newInfo->length();
        m_trackCount++;

        if( after ) {
            m_trackList.insert( pos+k, newInfo );
            if( isOpen() )
                after = new PlaylistTrackItem( this, after, newInfo );
        }
        else {
            if( m_loaded && !m_loading ) {
                m_trackList.append( newInfo );
                if( isOpen() )  //append the track item to the playlist
                    m_lastTrack = new PlaylistTrackItem( this, m_lastTrack, newInfo );
            }
            else
                tmp_droppedTracks.append( newInfo );
        }
        ++k;
    }

    if ( !m_loading ) {
        PlaylistBrowser::instance()->savePlaylist( this );
        if ( !m_loaded )
            tmp_droppedTracks.clear(); // after saving, dropped tracks are on the file
    }
}


void PlaylistEntry::removeTrack( Q3ListViewItem *item, bool isLast )
{
    #define item static_cast<PlaylistTrackItem*>(item)
    //remove a track and update playlist stats
    TrackItemInfo *info = item->trackInfo();
    m_length -= info->length();
    m_trackCount--;
    m_trackList.remove( info );
    if( item == m_lastTrack ) {
        Q3ListViewItem *above = item->itemAbove();
        m_lastTrack = above ? static_cast<PlaylistTrackItem *>( above ) : 0;
    }
    delete item;

    #undef item

    if( isLast )
        PlaylistBrowser::instance()->savePlaylist( this );
}


void PlaylistEntry::customEvent( QEvent *e )
{
    if( e->type() != (int)PlaylistReader::JobFinishedEventType )
        return;

#define playlist static_cast<PlaylistReader*>(e)
    QString str = playlist->title;

    if ( str.isEmpty() )
        str = fileBaseName( m_url.path() );

    str.replace( '_', ' ' );
    setText( 0, str );

    oldForeachType( BundleList, playlist->bundles )
    {
        const MetaBundle &b = *it;
        TrackItemInfo *info = new TrackItemInfo( b );
        m_trackList.append( info );
        m_length += info->length();
        if( isOpen() )
            m_lastTrack = new PlaylistTrackItem( this, m_lastTrack, info );
    }
#undef playlist

    //the tracks dropped on the playlist while it wasn't loaded are added to the track list
    if( tmp_droppedTracks.count() ) {

        for ( TrackItemInfo *info = tmp_droppedTracks.first(); info; info = tmp_droppedTracks.next() ) {
            m_trackList.append( info );
        }
        tmp_droppedTracks.clear();
    }

    m_loading = false;
    m_loaded = true;
    stopAnimation();  //stops the loading animation

    if( m_trackCount && !m_dynamic && !isDynamic() ) setOpen( true );
    else listView()->repaintItem( this );

    m_trackCount = m_trackList.count();
}

/**
 *  We destroy the tracks on collapsing the entry.  However, if we are using dynamic mode, then we leave them
 *  because adding from a custom list is problematic if the entry has no children.  Using load() is not effective
 *  since this is a threaded operation and would require pulling apart the entire class to make it work.
 */

void PlaylistEntry::setOpen( bool open )
{
    if( open == isOpen())
        return;

    if( open ) {    //expand

        if( m_loaded ) {
            //create track items
            for ( TrackItemInfo *info = m_trackList.first(); info; info = m_trackList.next() )
                m_lastTrack = new PlaylistTrackItem( this, m_lastTrack, info );
        }
        else if( !isDynamic() || !m_dynamic ) {
            load();
            return;
        }
    }
    else if( !isDynamic() || !m_dynamic ) {    //collapse

        //delete all children
        while( firstChild() )
            delete firstChild();

        m_lastTrack = 0;
    }

    Q3ListViewItem::setOpen( open );
    PlaylistBrowser::instance()->savePlaylists();
}


int PlaylistEntry::compare( Q3ListViewItem* i, int /*col*/ ) const
{
    PlaylistEntry* item = static_cast<PlaylistEntry*>(i);

    // Compare case-insensitive
    return QString::localeAwareCompare( text( 0 ).toLower(), item->text( 0 ).toLower() );
}


KUrl::List PlaylistEntry::tracksURL()
{
    KUrl::List list;

    if( m_loaded )  { //playlist loaded
        for( TrackItemInfo *info = m_trackList.first(); info; info = m_trackList.next() )
            list += info->url();
    }
    else
        list = m_url;    //playlist url

    return list;
}

void PlaylistEntry::updateInfo()
{
    const QString body = "<tr><td><b>%1</b></td><td>%2</td></tr>";

    QString str  = "<html><body><table width=\"100%\" border=\"0\">";

    str += body.arg( i18n( "Playlist" ),         text(0) );
    str += body.arg( i18n( "Number of tracks" ), QString::number(m_trackCount) );
    str += body.arg( i18n( "Length" ),           MetaBundle::prettyTime( m_length ) );
    str += body.arg( i18n( "Location" ),         m_url.prettyUrl() );
    str += "</table></body></html>";

    PlaylistBrowser::instance()->setInfo( text(0), str );
}

void PlaylistEntry::slotDoubleClicked()
{
    The::playlistModel()->proposePlaylistName( text(0), true );
    The::playlistModel()->insertMedia( url(), PlaylistNS::AppendAndPlay );
}


void PlaylistEntry::showContextMenu( const QPoint &position )
{
    KMenu menu( listView() );
    class MenuActions : public QObject
    {
        public:
            MenuActions(PlaylistEntry* parent)
            : QObject( parent )
            , m_parent( parent ) { }
        public slots:
            void load()
            {
                The::playlistModel()->clear();
                Playlist::instance()->setPlaylistName( m_parent->text(0), true );
                append();
            }
            void append() { PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Append ); }
            void queue()  { PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Queue );  }
            void mediaDeviceCopy() { MediaBrowser::queue()->addUrls( m_parent->tracksURL(), m_parent->text(0) ); }
            void mediaDeviceSync() { MediaBrowser::queue()->syncPlaylist( m_parent->text(0), m_parent->url() ); }
        private:
            PlaylistEntry* m_parent;
    };
    MenuActions mb(this);
    menu.addAction( KIcon( Amarok::icon( "files" ) ), i18n( "&Load" ), &mb, SLOT( load() ), Qt::Key_L );
    menu.addAction( KIcon( Amarok::icon( "add_playlist" ) ), i18n( "&Append to Playlist" ), &mb, SLOT( append() ) );
    menu.addAction( KIcon( Amarok::icon( "queue_track" ) ), i18n( "&Queue Tracks" ), &mb, SLOT( queue() ) );

    if( MediaBrowser::isAvailable() )
    {
        menu.addSeparator();
        menu.addAction( KIcon( Amarok::icon( "device" ) ),
                i18n( "&Transfer to Media Device" )   , &mb, SLOT( mediaDeviceCopy() ) );
        menu.addAction( KIcon( Amarok::icon( "device" ) ),
                i18n( "&Synchronize to Media Device" ), &mb, SLOT( mediaDeviceSync() ) );
    }

    menu.addSeparator();
    menu.addAction( KIcon( Amarok::icon("edit") ), i18n( "&Rename" ), PlaylistBrowser::instance(), SLOT( renameSelectedItem() ), Qt::Key_F2 );
    menu.addAction( KIcon( Amarok::icon("remove_from_playlist") ), i18n( "&Delete" ), PlaylistBrowser::instance(), SLOT( removeSelectedItems() ), Qt::ShiftModifier+Qt::Key_Delete );

    menu.exec( position );

}

void PlaylistEntry::slotPostRenameItem( const QString newName )
{
    QString oldPath = url().path();
    QString newPath = fileDirPath( oldPath ) + newName + '.' + Amarok::extension( oldPath );

    if ( std::rename( QFile::encodeName( oldPath ), QFile::encodeName( newPath ) ) == -1 )
        KMessageBox::error( listView(), i18n("Error renaming the file.") );
    else
        setUrl( newPath );
}

void PlaylistEntry::setDynamic( bool enable )
{
    if( enable != m_dynamic )
    {
        if( enable )
        {
            if( !m_loaded ) load(); // we need to load it to ensure that we can read the contents
            setPixmap( 0, SmallIcon( Amarok::icon( "favorites" ) ) );
        }
        else
            setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );

        m_dynamic = enable;
    }

    listView()->repaintItem( this );
}

void PlaylistEntry::setup()
{
    QFontMetrics fm( listView()->font() );
    int margin = listView()->itemMargin()*2;
    int h = fm.lineSpacing();
    if ( h % 2 > 0 ) h++;
    setHeight( h + margin );
}


void PlaylistEntry::paintCell( QPainter *p, const QColorGroup &cg, int column, int width, int align )
{
    //flicker-free drawing
    static QPixmap buffer;
    buffer.resize( width, height() );

    if( buffer.isNull() )
    {
        K3ListViewItem::paintCell( p, cg, column, width, align );
        return;
    }

    QPainter pBuf( &buffer );
    pBuf.setClipping( true );
    // use alternate background
    pBuf.fillRect( buffer.rect(), isSelected() ? cg.highlight() : backgroundColor(0) );

    K3ListView *lv = static_cast<K3ListView *>( listView() );

    QFont font( p->font() );
    QFontMetrics fm( p->fontMetrics() );

    int text_x = 0;// lv->treeStepSize() + 3;
    int textHeight;

    textHeight = height();

    pBuf.setPen( isSelected() ? cg.highlightedText() : cg.text() );

    if( pixmap( column ) )
    {
        int y = (textHeight - pixmap(column)->height())/2;
        pBuf.drawPixmap( text_x, y, *pixmap(column) );
        text_x += pixmap(column)->width()+4;
    }

    pBuf.setFont( font );
    QFontMetrics fmName( font );

    QString name = text(column);
    const int _width = width - text_x - lv->itemMargin()*2;
    name = fmName.elidedText( name, Qt::ElideRight, _width ); // use ... at the end if the text is longer that _width

    pBuf.drawText( text_x, 0, width - text_x, textHeight, Qt::AlignVCenter, name );

    pBuf.end();
    p->drawPixmap( 0, 0, buffer );
}


QDomElement PlaylistEntry::xml() const
{
        QDomDocument doc;
        QDomElement i = doc.createElement("playlist");
        i.setAttribute( "file", url().path() );
        i.setAttribute( "title", text(0) );
        if( isOpen() )
            i.setAttribute( "isOpen", "true" );

        QDomElement attr = doc.createElement( "tracks" );
        QDomText t = doc.createTextNode( QString::number( trackCount() ) );
        attr.appendChild( t );
        i.appendChild( attr );

        attr = doc.createElement( "length" );
        t = doc.createTextNode( QString::number( length() ) );
        attr.appendChild( t );
        i.appendChild( attr );

        QFileInfo fi( url().path() );
        attr = doc.createElement( "modified" );
        t = doc.createTextNode( QString::number( fi.lastModified().toTime_t() ) );
        attr.appendChild( t );
        i.appendChild( attr );

        return i;
}


//////////////////////////////////////////////////////////////////////////////////
///    CLASS PlaylistTrackItem
////////////////////////////////////////////////////////////////////////////////

PlaylistTrackItem::PlaylistTrackItem( Q3ListViewItem *parent, Q3ListViewItem *after, TrackItemInfo *info )
    : PlaylistBrowserEntry( parent, after )
    , m_trackInfo( info )
{
    setDragEnabled( true );
    setRenameEnabled( 0, false );
    PlaylistEntry *p = dynamic_cast<PlaylistEntry *>(parent);
    if(!p)
        debug() << "parent: " << parent << " is not a PlaylistEntry";
    if( p && p->text( 0 ).contains( info->artist() ) )
        setText( 0, info->title() );
    else
        setText( 0, i18n("%1 - %2", info->artist(), info->title() ) );
}

const KUrl &PlaylistTrackItem::url()
{
    return m_trackInfo->url();
}

void PlaylistTrackItem::slotDoubleClicked()
{
    The::playlistModel()->insertMedia( url(), PlaylistNS::AppendAndPlay );
}


void PlaylistTrackItem::showContextMenu( const QPoint &position )
{
    Q3PopupMenu menu( listView() );
    enum Actions { LOAD, APPEND, QUEUE, BURN, REMOVE, INFO };

    menu.insertItem( KIcon( Amarok::icon( "files" ) ), i18n( "&Load" ), LOAD );
    menu.insertItem( KIcon( Amarok::icon( "add_playlist" ) ), i18n( "&Append to Playlist" ), APPEND );
    menu.insertItem( KIcon( Amarok::icon( "queue_track" ) ), i18n( "&Queue Track" ), QUEUE );


    menu.addSeparator();

    menu.insertItem( KIcon( Amarok::icon( "burn" ) ), i18n("Burn to CD"), BURN );
    menu.setItemEnabled( BURN, K3bExporter::isAvailable() && url().isLocalFile() );

    menu.addSeparator();

    menu.insertItem( KIcon( Amarok::icon( "remove_from_playlist" ) ), i18n( "&Remove" ), REMOVE );
    menu.insertItem( KIcon( Amarok::icon( "info" ) ), i18nc( "[only-singular]", "Edit Track &Information..." ), INFO );

    switch( menu.exec( position ) ) {
        case LOAD:
            The::playlistModel()->clear(); //FALL THROUGH
        case APPEND:
            PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Append );
            break;
        case QUEUE:
            PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Queue );
            break;
        case BURN:
                K3bExporter::instance()->exportTracks( url() );
                break;
        case REMOVE:
            PlaylistBrowser::instance()->removeSelectedItems();
            break;
        case INFO:
            if( !url().isLocalFile() )
                KMessageBox::sorry( PlaylistBrowser::instance(), i18n( "Track information is not available for remote media." ) );
            else if( QFile::exists( url().path() ) ) {
                TagDialog* dialog = new TagDialog( url() );
                dialog->show();
            }
            else KMessageBox::sorry( PlaylistBrowser::instance(), i18n( "This file does not exist: %1" ).arg( url().path() ) );
    }
}


//////////////////////////////////////////////////////////////////////////////////
///    CLASS TrackItemInfo
////////////////////////////////////////////////////////////////////////////////

TrackItemInfo::TrackItemInfo( const MetaBundle &mb )
{
    m_url = mb.url();

    if( mb.isValidMedia() )
    {
        m_title  = mb.title();
        m_artist = mb.artist();
        m_album  = mb.album();
        m_length = mb.length();
    }
    else
        m_title = MetaBundle::prettyTitle( fileBaseName( m_url.path() ) );

    if( m_length < 0 )
        m_length = 0;
}

/////////////////////////////////////////////////////////////////////////////
///    CLASS StreamEntry
////////////////////////////////////////////////////////////////////////////

StreamEntry::StreamEntry( Q3ListViewItem *parent, Q3ListViewItem *after, const KUrl &u, const QString &t )
    : PlaylistBrowserEntry( parent, after )
    , m_title( t )
    , m_url( u )
{
    setDragEnabled( true );
    setRenameEnabled( 0, true );
    setExpandable( false );

    if( m_title.isEmpty() )
        m_title = fileBaseName( m_url.prettyUrl() );

    setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );

    setText( 0, m_title );
}

StreamEntry::StreamEntry( Q3ListViewItem *parent, Q3ListViewItem *after, const QDomElement &xmlDefinition )
    : PlaylistBrowserEntry( parent, after )
{
    setDragEnabled( true );
    setRenameEnabled( 0, true );
    setExpandable( false );

    m_title = xmlDefinition.attribute( "name" );
    QDomElement e = xmlDefinition.namedItem( "url" ).toElement();
    m_url  = KUrl( e.text() );


    if( m_title.isEmpty() )
        m_title = fileBaseName( m_url.prettyUrl() );

    setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );

    setText( 0, m_title );
}


QDomElement StreamEntry::xml() const
{
    QDomDocument doc;
    QDomElement i = doc.createElement("stream");
    i.setAttribute( "name", title() );
    if( isOpen() )
        i.setAttribute( "isOpen", "true" );
    QDomElement url = doc.createElement( "url" );
    url.appendChild( doc.createTextNode( m_url.prettyUrl() ));
    i.appendChild( url );
    return i;
}

void StreamEntry::updateInfo()
{
    const QString body = "<tr><td><b>%1</b></td><td>%2</td></tr>";

    QString str = "<html><body><table width=\"100%\" border=\"0\">";

    str += body.arg( i18n( "URL" ),  m_url.prettyUrl() );
    str += "</table></body></html>";

    PlaylistBrowser::instance()->setInfo( text(0), str );
}

void StreamEntry::slotDoubleClicked()
{
    Playlist::instance()->proposePlaylistName( text(0) );
    The::playlistModel()->insertMedia( url(), PlaylistNS::AppendAndPlay );
}

void StreamEntry::setup()
{
    QFontMetrics fm( listView()->font() );
    int margin = listView()->itemMargin()*2;
    int h = fm.lineSpacing();
    if ( h % 2 > 0 ) h++;
    setHeight( h + margin );
}

void StreamEntry::paintCell( QPainter *p, const QColorGroup &cg, int column, int width, int align )
{
    //flicker-free drawing
    static QPixmap buffer;
    buffer.resize( width, height() );

    if( buffer.isNull() )
    {
        K3ListViewItem::paintCell( p, cg, column, width, align );
        return;
    }

    QPainter pBuf( &buffer );
    pBuf.setClipping( true );
    // use alternate background
    pBuf.fillRect( buffer.rect(), isSelected() ? cg.highlight() : backgroundColor(0) );

    K3ListView *lv = static_cast<K3ListView *>( listView() );

    QFont font( p->font() );
    QFontMetrics fm( p->fontMetrics() );

    int text_x = 0;// lv->treeStepSize() + 3;
    int textHeight;

    textHeight = height();

    pBuf.setPen( isSelected() ? cg.highlightedText() : cg.text() );

    if( pixmap(column) ) {
        int y = (textHeight - pixmap(column)->height())/2;
        pBuf.drawPixmap( text_x, y, *pixmap(column) );
        text_x += pixmap(column)->width()+4;
    }

    pBuf.setFont( font );
    QFontMetrics fmName( font );

    QString name = text(column);
    const int _width = width - text_x - lv->itemMargin()*2;
    name = fmName.elidedText( name, Qt::ElideRight, _width ); // use ... at the end if the text is longer that _width

    pBuf.drawText( text_x, 0, width - text_x, textHeight, Qt::AlignVCenter, name );

    pBuf.end();
    p->drawPixmap( 0, 0, buffer );
}

void
StreamEntry::showContextMenu( const QPoint &position )
{
    Q3PopupMenu menu( listView() );
    enum Actions { LOAD, APPEND, QUEUE, EDIT, REMOVE };

    menu.insertItem( KIcon( Amarok::icon( "files" ) ), i18n( "&Load" ), LOAD );
    menu.insertItem( KIcon( Amarok::icon( "add_playlist" ) ), i18n( "&Append to Playlist" ), APPEND );
    menu.insertItem( KIcon( Amarok::icon( "queue_track" ) ), i18n( "&Queue Tracks" ), QUEUE );
    menu.addSeparator();

    // Forbid editing non removable items
    if( isKept() )
    {
        menu.insertItem( KIcon( Amarok::icon("edit") ), i18n( "E&dit" ), EDIT );
        menu.insertItem( KIcon( Amarok::icon("remove_from_playlist") ), i18n( "&Delete" ), REMOVE );
    }
    else
        menu.insertItem( KIcon( Amarok::icon( "info" ) ), i18n( "Show &Information" ), EDIT );

    switch( menu.exec( position ) )
    {
        case LOAD:
            The::playlistModel()->clear();
            Playlist::instance()->setPlaylistName( text(0) );
            //FALL THROUGH
        case APPEND:
            PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Append );
            break;
        case QUEUE:
            PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Queue );
            break;
        case EDIT:
            PlaylistBrowser::instance()->editStreamURL( this, !isKept() ); //only editable if we keep it
            if( dynamic_cast<LastFmEntry*>(this) )
                PlaylistBrowser::instance()->saveLastFm();
            else
                PlaylistBrowser::instance()->saveStreams();
            break;
        case REMOVE:
            PlaylistBrowser::instance()->removeSelectedItems();
            break;
    }
}


/////////////////////////////////////////////////////////////////////////////
///    CLASS LastFmEntry
////////////////////////////////////////////////////////////////////////////

QDomElement LastFmEntry::xml() const
{
    QDomDocument doc;
    QDomElement i = doc.createElement("lastfm");
    i.setAttribute( "name", title() );
    if( isOpen() )
        i.setAttribute( "isOpen", "true" );
    QDomElement url = doc.createElement( "url" );
    url.appendChild( doc.createTextNode( m_url.prettyUrl() ));
    i.appendChild( url );
    return i;
}

/////////////////////////////////////////////////////////////////////////////
///    CLASS StreamEditor
////////////////////////////////////////////////////////////////////////////

StreamEditor::StreamEditor( QWidget *parent, const QString &title, const QString &url, bool readonly )
    : KDialog( parent )
{
    setObjectName( "StreamEditor" );
    setModal( true );
    showButtonSeparator( true );

    //makeGridMainWidget( 2, Qt::Horizontal );

    QLabel *nameLabel = new QLabel( i18n("&Name:"), mainWidget() );
    m_nameLineEdit = new KLineEdit( title, mainWidget() );
    m_nameLineEdit->setReadOnly( readonly );
    nameLabel->setBuddy( m_nameLineEdit );

    QLabel *urlLabel = new QLabel( i18n("&Url:"), mainWidget() );
    m_urlLineEdit = new KLineEdit( url, mainWidget() );
    m_urlLineEdit->setReadOnly( readonly );
    urlLabel->setBuddy( m_urlLineEdit );

    if( !readonly ) {
        m_nameLineEdit->setFocus();
        setButtons( Ok | Cancel );
    }
    else
    {
        // In case of readonly ok button makes no sense
        setButtons( Close );
    }

    QSize minimum( 480, 110 );
    setInitialSize( minimum );
}


/////////////////////////////////////////////////////////////////////////////
///    CLASS DynamicEntry
////////////////////////////////////////////////////////////////////////////
DynamicEntry::DynamicEntry( Q3ListViewItem *parent, Q3ListViewItem *after, const QString &name )
        : PlaylistBrowserEntry( parent, after, name )
        , DynamicMode( name )
{
    setPixmap( 0, SmallIcon( Amarok::icon( "dynamic" ) ) );
    setDragEnabled( true );
}

DynamicEntry::DynamicEntry( Q3ListViewItem *parent, Q3ListViewItem *after, const QDomElement &xmlDefinition )
        : PlaylistBrowserEntry( parent, after )
        , DynamicMode( xmlDefinition.attribute( "name" ) )
{
    setPixmap( 0, SmallIcon( Amarok::icon( "dynamic" ) ) );
    setDragEnabled( true );

    QDomElement e;

    setCycleTracks  ( xmlDefinition.namedItem( "cycleTracks" ).toElement().text() == "true" );
    setUpcomingCount( xmlDefinition.namedItem( "upcoming" ).toElement().text().toInt() );
    setPreviousCount( xmlDefinition.namedItem( "previous" ).toElement().text().toInt() );

    setAppendType( xmlDefinition.namedItem( "appendType" ).toElement().text().toInt() );

    if ( appendType() == 2 ) {
        setItems( QStringList::split( ',', xmlDefinition.namedItem( "items" ).toElement().text() ) );
    }
}

QString DynamicEntry::text( int column ) const
{
    if( column == 0 )
        return title();
    return PlaylistBrowserEntry::text( column );
}

QDomElement DynamicEntry::xml() const
{
    QDomDocument doc;
    QDomElement i;

    i = doc.createElement("dynamic");
    i.setAttribute( "name", title() );
    if( isOpen() )
        i.setAttribute( "isOpen", "true" );

    QDomElement attr = doc.createElement( "cycleTracks" );
    QDomText t = doc.createTextNode( cycleTracks() ? "true" : "false" );
    attr.appendChild( t );
    i.appendChild( attr );

    attr = doc.createElement( "upcoming" );
    t = doc.createTextNode( QString::number( upcomingCount() ) );
    attr.appendChild( t );
    i.appendChild( attr );

    attr = doc.createElement( "previous" );
    t = doc.createTextNode( QString::number( previousCount() ) );
    attr.appendChild( t );
    i.appendChild( attr );

    attr = doc.createElement( "appendType" );
    t = doc.createTextNode( QString::number( appendType() ) );
    attr.appendChild( t );
    i.appendChild( attr );

    QString list;
    if( appendType() == 2 ) {
        QStringList itemsl = items();
        for( uint c = 0; c < itemsl.count(); c = c + 2 ) {
            list.append( itemsl[c] );
            list.append( ',' );
            list.append( itemsl[c+1] );
            if ( c < itemsl.count()-1 )
                list.append( ',' );
        }
    }

    attr = doc.createElement( "items" );
    t = doc.createTextNode( list );
    attr.appendChild( t );
    i.appendChild( attr );
    return i;
}

void
DynamicEntry::slotDoubleClicked()
{
    Playlist::instance()->loadDynamicMode( this );
    Playlist::instance()->setPlaylistName( text(0) );
}


void
DynamicEntry::showContextMenu( const QPoint &position )
{
    Q3PopupMenu menu( listView() );

    enum Actions { LOAD, RENAME, REMOVE, EDIT };
    menu.insertItem( KIcon( Amarok::icon( "files" ) ), i18n( "&Load" ), LOAD );
    menu.addSeparator();
    menu.insertItem( KIcon( Amarok::icon("edit") ), i18n( "E&dit" ), EDIT );
    menu.insertItem( KIcon( Amarok::icon("remove_from_playlist") ), i18n( "&Delete" ), REMOVE );

    if( !isKept() )
        menu.setItemEnabled( REMOVE, false );

    switch( menu.exec( position ) )
    {
        case LOAD:
            slotDoubleClicked();
            break;
        case EDIT:
            edit();
            break;
        case REMOVE:
            PlaylistBrowser::instance()->removeSelectedItems();
            break;
    }
}

/////////////////////////////////////////////////////////////////////////////
///    CLASS PodcastChannel
////////////////////////////////////////////////////////////////////////////

PodcastChannel::PodcastChannel( Q3ListViewItem *parent, Q3ListViewItem *after, const KUrl &url )
    : PlaylistBrowserEntry( parent, after )
        , m_polished( true ) // we get the items immediately if url is given
        , m_url( url )
        , m_fetching( false )
        , m_updating( false )
        , m_new( false )
        , m_hasProblem( false )
        , m_parent( static_cast<PlaylistCategory*>(parent) )
        , m_settingsValid( false )
{
    setDragEnabled( true );
    setRenameEnabled( 0, false );

    setText(0, i18n("Retrieving Podcast...") ); //HACK to fill loading time space
    setPixmap( 0, SmallIcon( Amarok::icon( "podcast" ) ) );

    fetch();
}

PodcastChannel::PodcastChannel( Q3ListViewItem *parent, Q3ListViewItem *after, const KUrl &url,
                                const QDomNode &channelSettings )
    : PlaylistBrowserEntry( parent, after )
    , m_polished( true ) // we get the items immediately if url is given
    , m_url( url )
    , m_fetching( false )
    , m_updating( false )
    , m_new( false )
    , m_hasProblem( false )
    , m_parent( static_cast<PlaylistCategory*>(parent) )
    , m_settingsValid( true )
{
    setDragEnabled( true );
    setRenameEnabled( 0, false );

    setDOMSettings( channelSettings );

    setText(0, i18n("Retrieving Podcast...") ); //HACK to fill loading time space
    setPixmap( 0, SmallIcon( Amarok::icon( "podcast" ) ) );

    fetch();
}

PodcastChannel::PodcastChannel( Q3ListViewItem *parent, Q3ListViewItem *after,
                                const KUrl &url, const QDomNode &channelSettings,
                                const QDomDocument &xmlDefinition )
    : PlaylistBrowserEntry( parent, after )
    , m_polished( true ) //automatically load the channel
    , m_url( url )
    , m_fetching( false )
    , m_updating( false )
    , m_new( false )
    , m_hasProblem( false )
    , m_parent( static_cast<PlaylistCategory*>(parent) )
    , m_settingsValid( true )
{
    QDomNode type = xmlDefinition.namedItem("rss");
    if( !type.isNull() )
        setXml( type.namedItem("channel"), RSS );
    else
        setXml( type, ATOM );

    setDOMSettings( channelSettings );

    setDragEnabled( true );
    setRenameEnabled( 0, false );

    setPixmap( 0, SmallIcon( Amarok::icon( "podcast" ) ) );
}

PodcastChannel::PodcastChannel( Q3ListViewItem *parent, Q3ListViewItem *after, const PodcastChannelBundle &pcb )
    : PlaylistBrowserEntry( parent, after )
    , m_bundle( pcb )
    , m_polished( false )
    , m_url( pcb.url() )
    , m_fetching( false )
    , m_updating( false )
    , m_new( false )
    , m_hasProblem( false )
    , m_parent( static_cast<PlaylistCategory*>(parent) )
    , m_settingsValid( true )
{
    setText( 0, title() );
    setDragEnabled( true );
    setRenameEnabled( 0, false );
    setPixmap( 0, SmallIcon( Amarok::icon( "podcast" ) ) );
    setExpandable( true );
}

void
PodcastChannel::setDOMSettings( const QDomNode &channelSettings )
{
    QString save   = channelSettings.namedItem("savelocation").toElement().text();
    bool scan      = channelSettings.namedItem("autoscan").toElement().text() == "true";
    bool hasPurge  = channelSettings.namedItem("purge").toElement().text() == "true";
    int purgeCount = channelSettings.namedItem("purgecount").toElement().text().toInt();
    int fetchType  = STREAM;

    if( channelSettings.namedItem( "fetch").toElement().text() == "automatic" )
        fetchType  = AUTOMATIC;

    KUrl saveURL;
    QString t = title();
    if( save.isEmpty() )
        save = Amarok::saveLocation( "podcasts/" + Amarok::vfatPath( t ) );

    PodcastSettings *settings = new PodcastSettings( t, save, scan, fetchType, false/*transfer*/, hasPurge, purgeCount );
    m_bundle.setSettings( settings );
}

void
PodcastChannel::configure()
{
    PodcastSettingsDialog *dialog = new PodcastSettingsDialog( m_bundle.getSettings() );

    if( dialog->configure() )
    {
        setSettings( dialog->getSettings() );
    }

    delete dialog->getSettings();
    delete dialog;
}

void
PodcastChannel::checkAndSetNew()
{
    for( Q3ListViewItem *child = firstChild(); child; child = child->nextSibling() )
    {
        if( static_cast<PodcastEpisode*>(child)->isNew() )
        {
            setNew( true );
            return;
        }
    }
    setNew( false );
}

void
PodcastChannel::setListened( const bool n /*true*/ )
{
    if( !isPolished() )
        load();

    Q3ListViewItem *child = firstChild();
    while( child )
    {
        static_cast<PodcastEpisode*>(child)->setListened( n );
        child = child->nextSibling();
    }

    setNew( !n );
}

void
PodcastChannel::setOpen( bool b )
{
    if( b == isOpen())
        return;

    if( isPolished() )
    {
        Q3ListViewItem::setOpen( b );
        return;
    }
    // not polished
    if( b ) load();
    Q3ListViewItem::setOpen( b );
}

void
PodcastChannel::load()
{
    m_polished = true;

    bool hasNew = m_new;
    int episodeCount = hasPurge() ? purgeCount() : -1;
    Q3ValueList<PodcastEpisodeBundle> episodes;
    episodes = CollectionDB::instance()->getPodcastEpisodes( url(), false, episodeCount );

    PodcastEpisodeBundle bundle;

    // podcasts are hopefully returned chronologically, insert them in reverse
    while( !episodes.isEmpty() )
    {
        bundle = episodes.first();
        new PodcastEpisode( this, 0, bundle );

        if( bundle.isNew() )
            hasNew = true;

        episodes.pop_front();
    }
    sortChildItems( 0, true );
    setNew( hasNew );
}

void
PodcastChannel::setSettings( PodcastSettings *newSettings )
{
    bool downloadMedia = ( (fetchType() != newSettings->fetchType()) && (newSettings->fetchType() == AUTOMATIC) );

    /**
     * Rewrite local url
     * Move any downloaded media to the new location
     */
    if( saveLocation() != newSettings->saveLocation() )
    {
        KUrl::List copyList;

        PodcastEpisode *item = static_cast<PodcastEpisode*>( firstChild() );
            // get a list of the urls of already downloaded items
        while( item )
        {
            if( item->isOnDisk() )
            {
                copyList << item->localUrl();
                item->setLocalUrlBase( newSettings->saveLocation() );
            }
            item = static_cast<PodcastEpisode*>( item->nextSibling() );
        }
            // move the items
        if( !copyList.isEmpty() )
        {
            //create the local directory first
            PodcastEpisode::createLocalDir( newSettings->saveLocation() );
            KIO::CopyJob* m_podcastMoveJob = KIO::move( copyList, KUrl( newSettings->saveLocation() ), false );
            Amarok::StatusBar::instance()->newProgressOperation( m_podcastMoveJob )
                    .setDescription( i18n( "Moving Podcasts" ) );
        }
    }

    if( newSettings->autoscan() != autoscan() )
    {
        if( autoscan() )
            PlaylistBrowser::instance()->m_podcastItemsToScan.append( this );
        else
            PlaylistBrowser::instance()->m_podcastItemsToScan.remove( this );
    }

    m_bundle.setSettings( newSettings );
    CollectionDB::instance()->updatePodcastChannel( m_bundle );

    if( hasPurge() && purgeCount() != childCount() && purgeCount() != 0 )
        purge();

    if( downloadMedia )
        downloadChildren();
}

void
PodcastChannel::downloadChildren()
{
    Q3ListViewItem *item = firstChild();
    while( item )
    {
        #define item static_cast<PodcastEpisode*>(item)
        if( item->isNew() )
            m_podcastDownloadQueue.append( item );
        #undef  item

        item = item->nextSibling();
    }
    downloadChildQueue();
}

void
PodcastChannel::downloadChildQueue()
{
    if( m_podcastDownloadQueue.isEmpty() ) return;

    PodcastEpisode *first = m_podcastDownloadQueue.first();
    first->downloadMedia();
    m_podcastDownloadQueue.removeFirst();

    connect( first, SIGNAL( downloadFinished() ), this, SLOT( downloadChildQueue() ) );
}

void
PodcastChannel::fetch()
{
    setText( 0, i18n( "Retrieving Podcast..." ) );

    m_iconCounter = 1;
    startAnimation();
    connect( &m_animationTimer, SIGNAL(timeout()), this, SLOT(slotAnimation()) );

    m_podcastJob = KIO::storedGet( m_url, false, false );

    Amarok::StatusBar::instance()->newProgressOperation( m_podcastJob )
        .setDescription( i18n( "Fetching Podcast" ) )
        .setAbortSlot( this, SLOT( abortFetch() ) );

    connect( m_podcastJob, SIGNAL( result( KJob* ) ), SLOT( fetchResult( KJob* ) ) );
}

void
PodcastChannel::abortFetch()
{
    m_podcastJob->kill();

    stopAnimation();
    title().isEmpty() ?
        setText( 0, m_url.prettyUrl() ) :
        setText( 0, title() );
}

void
PodcastChannel::fetchResult( KJob* job ) //SLOT
{
    stopAnimation();
    if ( job->error() != 0 )
    {
        Amarok::StatusBar::instance()->shortMessage( i18n( "Unable to connect to Podcast server." ) );
        debug() << "Unable to retrieve podcast information. KIO Error: " << job->error();

        title().isEmpty() ?
            setText( 0, m_url.prettyUrl() ) :
            setText( 0, title() );
        setPixmap( 0, SmallIcon("cancel") );

        return;
    }

    KIO::StoredTransferJob* const storedJob = static_cast<KIO::StoredTransferJob*>( job );

    QDomDocument d;

    QString data = QString( storedJob->data() );
    QString error;
    int errorline, errorcolumn;
    if( !d.setContent( storedJob->data(), false /* disable namespace processing */,
                &error, &errorline, &errorcolumn ) )
    {
        Amarok::StatusBar::instance()->shortMessage( i18n("Podcast returned invalid data.") );
        debug() << "Podcast DOM failure in line " << errorline << ", column " << errorcolumn << ": " << error;

        title().isEmpty() ?
            setText( 0, m_url.prettyUrl() ) :
            setText( 0, title() );
        setPixmap( 0, SmallIcon("cancel") );
        return;
    }

    QDomNode type = d.namedItem("rss");
    if( type.isNull() || type.toElement().attribute( "version" ) != "2.0" )
    {
        type = d.namedItem("feed");
        if( type.isNull() )
        {
            Amarok::StatusBar::instance()->shortMessage( i18n("Sorry, only RSS 2.0 or Atom feeds for podcasts!") );

            if( title().isEmpty() )
                setText( 0, m_url.prettyUrl() );

            setPixmap( 0, SmallIcon("cancel") );
            return;
        }
        // feed is ATOM
        else
        {
            setXml( type, ATOM );
        }
    }
    // feed is rss 2.0
    else
        setXml( type.namedItem("channel"), RSS );
}

void
PodcastChannel::removeChildren()
{
    Q3ListViewItem *child, *next;
    if ( (child = firstChild()) )
    {
        while ( (next = child->nextSibling()) )
        {
            delete child;
            child=next;
        }
        delete child;
    }
}

void
PodcastChannel::rescan()
{
    m_updating = true;
    fetch();
}

void
PodcastChannel::setNew( bool n )
{
    if( n )
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast2" ) ) );
    else if( m_hasProblem )
        setPixmap( 0, SmallIcon("cancel") );
    else
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast" ) ) );

    m_new = n;
}


/// DON'T TOUCH m_url!!!  The podcast has no mention to the location of the xml file.
void
PodcastChannel::setXml( const QDomNode &xml, const int feedType )
{
    /// Podcast Channel information
    const bool isAtom = ( feedType == ATOM );

    QString t = xml.namedItem( "title" ).toElement().text().remove("\n");

    QString a = xml.namedItem( "author" ).toElement().text().remove("\n");

    setText( 0, t );

    QString l = QString();

    if( isAtom )
        l = xml.namedItem( "link" ).toElement().attribute( "rel" );
    else
        l = xml.namedItem( "link" ).toElement().text();

    QString d = xml.namedItem( "description" ).toElement().text();
    QString id = xml.namedItem( "itunes:summary" ).toElement().text();
    if( id.length() > d.length() )
       d = id;
    QString c = xml.namedItem( "copyright" ).toElement().text();
    QString img = xml.namedItem( "image" ).toElement().namedItem( "url" ).toElement().text();
    if( img.isEmpty() )
        img = xml.namedItem( "itunes:image" ).toElement().namedItem( "url" ).toElement().text();
    if( img.isEmpty() )
        img = xml.namedItem( "itunes:image" ).toElement().attribute( "href" );
    if( img.isEmpty() )
        img = xml.namedItem( "itunes:image" ).toElement().text();

    PodcastSettings * settings = 0;
    if( m_settingsValid )
    {
        settings = m_bundle.getSettings();
    }
    else
    {
        settings = new PodcastSettings( t );
        m_settingsValid = true;
    }

    m_bundle = PodcastChannelBundle( m_url, t, a, l, d, c, settings );
    delete settings;
    m_bundle.setImageURL( KUrl( img ) );

    m_bundle.setParentId( m_parent->id() );
    if( !m_updating )
    { // don't reinsert on a refresh
        debug() << "Adding podcast to database";
        CollectionDB::instance()->addPodcastChannel( m_bundle );
    }
    else
    {
        debug() << "Updating podcast in database: ";
        CollectionDB::instance()->updatePodcastChannel( m_bundle );
    }

    /// Podcast Episodes information

    QDomNode n;
    if( isAtom )
        n = xml.namedItem( "entry" );
    else
        n = xml.namedItem( "item" );

    bool hasNew = false;
    bool downloadMedia = ( fetchType() == AUTOMATIC );
    QDomNode node;

    // We use an auto-increment id in the database, so we must insert podcasts in the reverse order
    // to ensure we can pull them out reliably.

    Q3PtrList<QDomElement> eList;

    for( ; !n.isNull(); n = n.nextSibling() )
    {
        if( !n.namedItem( "enclosure" ).toElement().attribute( "url" ).isEmpty() )
        {
            //prepending ensures correct order in 99% of the channels, except those who use chronological order
            eList.prepend( new QDomElement( n.toElement() ) );
        }
        else if( isAtom )
        {
            // Atom feeds have multiple nodes called link, only one which has an enclosure.
            QDomNode nodes = n.namedItem("link");
            for( ; !nodes.isNull(); nodes = nodes.nextSibling() )
            {
                if( nodes.toElement().attribute("rel") == "enclosure" )
                {
                    eList.prepend( new QDomElement( n.toElement() ) );
                    break;
                }
            }
        }
    }

    uint i = m_bundle.hasPurge() ? m_bundle.purgeCount() : eList.count();
    oldForeachType( Q3PtrList<QDomElement>, eList )
    {
        if( !m_updating || ( ( i++ >= eList.count() ) && !episodeExists( (**it), feedType ) ) )
        {
            if( !isPolished() )
                load();
            PodcastEpisode *ep = new PodcastEpisode( this, 0, (**it), feedType, m_updating/*new*/ );
            if( m_updating )
            {
                ep->setNew( true );
                hasNew = true;
            }
        }
    }

     if( hasPurge() && purgeCount() != 0 && childCount() > purgeCount() )
         purge();

    //sortChildItems( 0, true ); // ensure the correct date order

    if( downloadMedia )
        downloadChildren();

    if( m_updating && hasNew )
    {
        setNew();
        Amarok::StatusBar::instance()->shortMessage( i18n("New podcasts have been retrieved!") );
    }
}

const bool
PodcastChannel::episodeExists( const QDomNode &xml, const int feedType )
{
    QString command;
    if( feedType == RSS )
    {
        //check id
        QString guid = xml.namedItem( "guid" ).toElement().text();
        if( !guid.isEmpty() )
        {
            command = QString("SELECT id FROM podcastepisodes WHERE parent='%1' AND guid='%2';")
                              .arg( CollectionDB::instance()->escapeString( url().url() ),
                                    CollectionDB::instance()->escapeString( guid ) );
            QStringList values = CollectionDB::instance()->query( command );
            return !values.isEmpty();
        }

        QString episodeTitle = xml.namedItem( "title" ).toElement().text();
        KUrl episodeURL      = xml.namedItem( "enclosure" ).toElement().attribute( "url" );
        command = QString("SELECT id FROM podcastepisodes WHERE parent='%1' AND url='%2' AND title='%3';")
                          .arg( CollectionDB::instance()->escapeString( url().url() ),
                                CollectionDB::instance()->escapeString( episodeURL.url() ),
                                CollectionDB::instance()->escapeString( episodeTitle ) );
        QStringList values = CollectionDB::instance()->query( command );
        return !values.isEmpty();
    }

    else if( feedType == ATOM )
    {
        //check id
        QString guid = xml.namedItem( "id" ).toElement().text();
        if( !guid.isEmpty() )
        {
            command = QString("SELECT id FROM podcastepisodes WHERE parent='%1' AND guid='%2';")
                              .arg( CollectionDB::instance()->escapeString( url().url() ),
                                    CollectionDB::instance()->escapeString( guid ) );
            QStringList values = CollectionDB::instance()->query( command );
            return !values.isEmpty();
        }

        QString episodeTitle = xml.namedItem("title").toElement().text();
        QString episodeURL = QString();
        QDomNode n = xml.namedItem("link");
        for( ; !n.isNull(); n = n.nextSibling() )
        {
            if( n.nodeName() == "link" && n.toElement().attribute("rel") == "enclosure" )
            {
                episodeURL = n.toElement().attribute( "href" );
                break;
            }
        }

        command = QString("SELECT id FROM podcastepisodes WHERE parent='%1' AND url='%2' AND title='%3';")
                          .arg( CollectionDB::instance()->escapeString( url().url() ),
                                CollectionDB::instance()->escapeString( episodeURL ),
                                CollectionDB::instance()->escapeString( episodeTitle ) );
        QStringList values = CollectionDB::instance()->query( command );

        return !values.isEmpty();
    }

    return false;
}

void
PodcastChannel::setParent( PlaylistCategory *newParent )
{
    if( newParent != m_parent )
    {
        m_parent->takeItem( this );
        newParent->insertItem( this );
        newParent->sortChildItems( 0, true );

        m_parent = newParent;
    }
    m_bundle.setParentId( m_parent->id() );

    CollectionDB::instance()->updatePodcastChannel( m_bundle );
}

void
PodcastChannel::updateInfo()
{
    if( !isPolished() )
            load();

    const QString body = "<tr><td><b>%1</b></td><td>%2</td></tr>";

    QString str  = "<html><body><table width=\"100%\" border=\"0\">";

    str += body.arg( i18n( "Description" ), description() );
    str += body.arg( i18n( "Website" ),     link().prettyUrl() );
    str += body.arg( i18n( "Copyright" ),   copyright() );
    str += body.arg( i18n( "URL" ),         m_url.prettyUrl() );
    str += "</table>";
    str += i18n( "<p>&nbsp;<b>Episodes</b></p><ul>" );
    for( Q3ListViewItem *c = firstChild(); c; c = c->nextSibling() )
    {
        str += QString("<li>%1</li>").arg( static_cast<PodcastEpisode*>(c)->title() );
    }

    str += "</ul></body></html>";

    PlaylistBrowser::instance()->setInfo( text(0), str );
}

void
PodcastChannel::slotDoubleClicked()
{
    if( !isPolished() )
            load();
    KUrl::List list;
    Q3ListViewItem *child = firstChild();
    while( child )
    {
        #define child static_cast<PodcastEpisode *>(child)
        child->isOnDisk() ?
            list.prepend( child->localUrl() ):
            list.prepend( child->url()      );
        #undef child
        child = child->nextSibling();
    }

    Playlist::instance()->proposePlaylistName( text(0) );
    The::playlistModel()->insertMedia( list, PlaylistNS::AppendAndPlay );
    setNew( false );
}

//maintain max items property
void
PodcastChannel::purge()
{
    // if the user wants to increase the max items shown, we should find those items and add them
    // back to the episode list.
    if( childCount() - purgeCount() <= 0 )
    {
        restorePurged();
        return;
    }

    KUrl::List urlsToDelete;
    Q3ValueList<Q3ListViewItem*> purgedItems;

    Q3ListViewItem *current = firstChild();
    for( int i=0; current && i < childCount(); current = current->nextSibling(), i++ )
    {
        if( i < purgeCount() )
            continue;

        purgedItems.append( current );
    }

    oldForeachType( Q3ValueList<Q3ListViewItem*>, purgedItems )
    {
        Q3ListViewItem *item = *it;

    #define item static_cast<PodcastEpisode*>(item)
        if( item->isOnDisk() )
            urlsToDelete.append( item->localUrl() );

//         CollectionDB::instance()->removePodcastEpisode( item->dBId() );
        m_podcastDownloadQueue.remove( item );
    #undef  item
        delete item;
    }

    if( !urlsToDelete.isEmpty() )
        KIO::del( urlsToDelete );
}

void
PodcastChannel::restorePurged()
{
    DEBUG_BLOCK
    int restoreCount = purgeCount() - childCount();

    if( restoreCount <= 0 ) return;

    Q3ValueList<PodcastEpisodeBundle> episodes;
    episodes = CollectionDB::instance()->getPodcastEpisodes( url() );

    Q3ValueList<PodcastEpisodeBundle> possibleEntries;

    int i = 0;

    // qvaluelist has no reverse iterator :-(
    for( ; !episodes.isEmpty(); )
    {
        PodcastEpisodeBundle episode = episodes.last();
        if ( i >= restoreCount ) break;

        PodcastEpisode *existingItem = static_cast<PodcastEpisode*>( firstChild() );
        bool skip = false;
        while ( existingItem )
        {
            if ( episode.url()   == existingItem->url()   &&
                 episode.title() == existingItem->title() &&
                 episode.date()  == existingItem->date()  &&
                 episode.guid()  == existingItem->guid() ) {
                skip = true;
                break;
            }
            existingItem = static_cast<PodcastEpisode*>( existingItem->nextSibling() );
        }
        if( !skip )
        {
            possibleEntries.append( episode );
            i++;
        }
        episodes.pop_back();
    }

    // the sorting of the channels automatically means the new episodes gets placed at the end
    for( Q3ValueList<PodcastEpisodeBundle>::Iterator it = possibleEntries.begin(), end = possibleEntries.end();
         it != end; ++it )
        new PodcastEpisode( this, 0, (*it) );

    sortChildItems( 0, true );
}

void
PodcastChannel::startAnimation()
{
    if( !m_animationTimer.isActive() )
        m_animationTimer.start( ANIMATION_INTERVAL );
}

void
PodcastChannel::stopAnimation()
{
    m_animationTimer.stop();

    hasNew() ?
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast2" ) ) ):
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast"  ) ) );
}

void
PodcastChannel::slotAnimation()
{
    m_iconCounter % 2 ?
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast" ) ) ):
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast2" ) ) );

    m_iconCounter++;
}

void
PodcastChannel::showContextMenu( const QPoint &position )
{
    Q3PopupMenu menu( listView() );
    
    enum AMAROK_Actions { ACTIONS_LOAD, ACTIONS_APPEND, ACTIONS_QUEUE, 
        ACTIONS_DELETE, ACTIONS_RESCAN, ACTIONS_LISTENED, ACTIONS_NEW, ACTIONS_CONFIG };

    menu.insertItem( KIcon( Amarok::icon( "files" ) ), i18n( "&Load" ), ACTIONS_LOAD );
    menu.insertItem( KIcon( Amarok::icon( "add_playlist" ) ), i18n( "&Append to Playlist" ), ACTIONS_APPEND );
    menu.insertItem( KIcon( Amarok::icon( "queue_track" ) ), i18n( "&Queue Tracks" ), ACTIONS_QUEUE );
    menu.addSeparator();
    menu.insertItem( KIcon( Amarok::icon( "remove" ) ), i18n( "&Delete" ), ACTIONS_DELETE );
    menu.insertItem( KIcon( Amarok::icon( "refresh" ) ), i18n( "&Check for Updates" ), ACTIONS_RESCAN );
    menu.insertItem( KIcon( Amarok::icon( "artist" ) ), i18n( "Mark as &Listened" ), ACTIONS_LISTENED );
    menu.insertItem( KIcon( Amarok::icon( "artist" ) ), i18n( "Mark as &New" ), ACTIONS_NEW );
    menu.insertItem( KIcon( Amarok::icon( "configure" ) ), i18n( "&Configure..." ), ACTIONS_CONFIG );
    menu.setItemEnabled( ACTIONS_LISTENED, hasNew() );
    menu.setItemEnabled( ACTIONS_CONFIG, m_settingsValid );

    switch( menu.exec( position ) )
    {
        case ACTIONS_LOAD:
            The::playlistModel()->clear();
            Playlist::instance()->setPlaylistName( text(0) );
            //FALL THROUGH
        case ACTIONS_APPEND:
            PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Append );
            break;

        case ACTIONS_QUEUE:
            PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Queue );
            break;

        case ACTIONS_RESCAN:
            rescan();
            break;

        case ACTIONS_LISTENED:
            setListened();
            break;

        case ACTIONS_NEW:
            setListened(false);
            break;
        case ACTIONS_DELETE:
            PlaylistBrowser::instance()->removeSelectedItems();
            break;

        case ACTIONS_CONFIG:
        {
            PlaylistBrowser::instance()->configureSelectedPodcasts();
            break;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
///    CLASS PodcastEpisode
///    @note we fucking hate itunes for taking over podcasts and inserting
///          their own attributes.
////////////////////////////////////////////////////////////////////////////
PodcastEpisode::PodcastEpisode( Q3ListViewItem *parent, Q3ListViewItem *after,
                                const QDomElement &xml, const int feedType, const bool &isNew )
    : PlaylistBrowserEntry( parent, after )
      , m_parent( parent )
      , m_fetching( false )
      , m_onDisk( false )
{
    const bool isAtom = ( feedType == ATOM );
    QString title = xml.namedItem( "title" ).toElement().text().remove("\n");
    QString subtitle;

    QString description, author, date, guid, type;
    int duration = 0;
    uint size = 0;
    KUrl link;

    if( isAtom )
    {
        for( QDomNode n = xml.firstChild(); !n.isNull(); n = n.nextSibling() )
        {
            if      ( n.nodeName() == "summary" )   description = n.toElement().text();
            else if ( n.nodeName() == "author" )    author      = n.toElement().text().remove("\n");
            else if ( n.nodeName() == "published" ) date        = n.toElement().text();
            else if ( n.nodeName() == "id" )        guid        = n.toElement().text();
            else if ( n.nodeName() == "link" )
            {
                if( n.toElement().attribute( "rel" ) == "enclosure" )
                {
                    const QString weblink = n.toElement().attribute( "href" );
                    link = KUrl( weblink );
                }
            }
        }
    }
    else
    {
        description = xml.namedItem( "description" ).toElement().text();
        QString idescription = xml.namedItem( "itunes:summary" ).toElement().text();
        if( idescription.length() > description.length() )
           description = idescription;

        if( subtitle.isEmpty() )
            subtitle = xml.namedItem( "itunes:subtitle" ).toElement().text();

        author   = xml.namedItem( "author" ).toElement().text().remove("\n");
        if( author.isEmpty() )
            author = xml.namedItem( "itunes:author" ).toElement().text().remove("\n");

        date     = xml.namedItem( "pubDate" ).toElement().text();
        if( date.isEmpty() )
            date = xml.namedItem( "dc:date" ).toElement().text();

        QString ds = xml.namedItem( "itunes:duration" ).toElement().text();
        QString secs = ds.section( ":", -1, -1 );
        duration = secs.toInt();
        QString min = ds.section( ":", -2, -2 );
        duration += min.toInt() * 60;
        QString h = ds.section( ":", -3, -3 );
        duration += h.toInt() * 3600;

        size     = xml.namedItem( "enclosure" ).toElement().attribute( "length" ).toInt();
        type     = xml.namedItem( "enclosure" ).toElement().attribute( "type" );
        guid     = xml.namedItem( "guid" ).toElement().text();

        const QString weblink = xml.namedItem( "enclosure" ).toElement().attribute( "url" );

        link     = KUrl( weblink );
    }

    if( title.isEmpty() )
        title = link.fileName();

    KUrl parentUrl = static_cast<PodcastChannel*>(parent)->url();
    m_bundle.setDBId( -1 );
    m_bundle.setUrl( link );
    m_bundle.setParent( parentUrl );
    m_bundle.setTitle( title );
    m_bundle.setSubtitle( subtitle );
    m_bundle.setAuthor( author );
    m_bundle.setDescription( description );
    m_bundle.setDate( date );
    m_bundle.setType( type );
    m_bundle.setDuration( duration );
    m_bundle.setSize( size );
    m_bundle.setGuid( guid );
    m_bundle.setNew( isNew );

    int id = CollectionDB::instance()->addPodcastEpisode( m_bundle );
    m_bundle.setDBId( id );

    setText( 0, title );
    updatePixmap();
    setDragEnabled( true );
    setRenameEnabled( 0, false );
}

PodcastEpisode::PodcastEpisode( Q3ListViewItem *parent, Q3ListViewItem *after, PodcastEpisodeBundle &bundle )
    : PlaylistBrowserEntry( parent, after )
      , m_parent( parent )
      , m_bundle( bundle )
      , m_fetching( false )
      , m_onDisk( false )
{
    m_localUrl    =  m_bundle.localUrl();
    isOnDisk();

    setText( 0, bundle.title() );
    updatePixmap();
    setDragEnabled( true );
    setRenameEnabled( 0, false );
}

int
PodcastEpisode::compare( Q3ListViewItem* item, int col, bool ascending ) const
{
    if ( item->rtti() == PodcastEpisode::RTTI )
    {
        int ret;
        #define item static_cast<PodcastEpisode*>(item)
        // date is priority
        bool thisHasDate = m_bundle.dateTime().isValid();
        bool thatHasDate = item->m_bundle.dateTime().isValid();
        if( thisHasDate && thatHasDate )
        {
            ret = m_bundle.dateTime() < item->m_bundle.dateTime() ? 1 : -1;
            if ( !ascending )  ret *= -1;
            return ret;
        }

        // if neither has a date, then we order upon the id in the database.  This
        // should be the order in which it arrives in the feed.
        if( !thisHasDate && !thatHasDate )
        {
            ret = m_bundle.dBId() < item->m_bundle.dBId() ?  1 : -1;
            if ( !ascending )  ret *= -1;
            return ret;
        }

        // if one has a date, and the other doesn't, always keep non-dated at the bottom.
        // hypothetically, this should never happen, but it might.
        ret = thisHasDate ? 1 : -1;
        if ( !ascending )  ret *= -1;
        return ret;
        #undef item
    }

    return PlaylistBrowserEntry::compare( item, col, ascending );
}

void
PodcastEpisode::updatePixmap()
{
    if( isNew() )
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast2" ) ) );
    else if( m_onDisk )
        setPixmap( 0, SmallIcon( "down" ) );
    else
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast" ) ) );
}

const bool
PodcastEpisode::isOnDisk()
{
    if( m_localUrl.isEmpty() )
        return false;
    else
    {
        bool oldOnDisk = m_onDisk;
        m_onDisk = QFile::exists( m_localUrl.path() );
        updatePixmap();
        m_bundle.setLocalURL( m_onDisk ? m_localUrl : KUrl() );
        if( oldOnDisk != m_onDisk && dBId() )
            CollectionDB::instance()->updatePodcastEpisode( dBId(), m_bundle );
        return m_onDisk;
    }
}

void
PodcastEpisode::downloadMedia()
{
    DEBUG_BLOCK
    DEBUG_THREAD_FUNC_INFO
    SHOULD_BE_GUI

    if( isOnDisk() )
        return;

    setText( 0, i18n( "Downloading Media..." ) );

    m_iconCounter = 1;
    startAnimation();
    connect( &m_animationTimer, SIGNAL(timeout()), this, SLOT(slotAnimation()) );

    KUrl m_localDir;
    PodcastChannel *channel = dynamic_cast<PodcastChannel*>(m_parent);
    if( channel )
        m_localDir = KUrl( channel->saveLocation() );
    else
        m_localDir = KUrl( PodcastSettings("Podcasts").saveLocation() );
    createLocalDir( m_localDir );

    //filename might get changed by redirects later.
    m_filename = url().fileName();
    m_localUrl = m_localDir;
    m_podcastEpisodeJob = KIO::storedGet( url().url(), false, false);

    Amarok::StatusBar::instance()->newProgressOperation( m_podcastEpisodeJob )
            .setDescription( title().isEmpty()
                    ? i18n( "Downloading Podcast Media" )
                    : i18n( "Downloading Podcast \"%1\"", title() ) )
            .setAbortSlot( this, SLOT( abortDownload()) )
            .setProgressSignal( m_podcastEpisodeJob, SIGNAL( percent( KJob *, unsigned long ) ) );

    connect( m_podcastEpisodeJob, SIGNAL(  result( KJob * ) ), SLOT( downloadResult( KJob * ) ) );
    connect( m_podcastEpisodeJob, SIGNAL( redirection( KIO::Job *,const KUrl& ) ), SLOT( redirected( KIO::Job *,const KUrl& ) ) );
}

/* change the localurl if redirected, allows us to use the original filename to transfer to mediadevices*/
void PodcastEpisode::redirected( KIO::Job *, const KUrl & redirectedUrl )
{
    DEBUG_BLOCK
    debug() << "redirecting to " << redirectedUrl << ". filename: " << redirectedUrl.fileName();
    m_filename = redirectedUrl.fileName();
}

void PodcastEpisode::createLocalDir( const KUrl &localDir )
{
    if( localDir.isEmpty() ) return;

    QString localDirString = localDir.path();
    if( !QFile::exists( localDirString ) )
    {
        QString parentDirString = localDir.directory();
        createLocalDir( parentDirString );
        QDir dir( localDirString );
        dir.mkdir( localDirString );
    }
}

void
PodcastEpisode::abortDownload() //SLOT
{
    emit downloadAborted();
    if( m_podcastEpisodeJob )
        m_podcastEpisodeJob->kill( KJob::EmitResult );

    //don't delete m_podcastFetcher yet, kill() is async
    stopAnimation();
    setText( 0, title() );
    m_onDisk = false;
    updatePixmap();
}

void PodcastEpisode::downloadResult( KJob * transferJob )
{
    DEBUG_BLOCK
    emit downloadFinished();
    stopAnimation();
    setText( 0, title() );

    if( transferJob->error() )
    {
        Amarok::StatusBar::instance()->shortMessage( i18n( "Media download aborted, unable to connect to server." ) );
        debug() << "Unable to retrieve podcast media. KIO Error: " << transferJob->error();

        setPixmap( 0, SmallIcon("cancel") );
        return;
    }

    m_localUrl.addPath( m_filename );
    debug() << "filename: " << m_localUrl.path();
    QFile *localFile = new QFile( m_localUrl.path() );
    localFile->open( QIODevice::WriteOnly );
    localFile->write( m_podcastEpisodeJob->data() );
    localFile->close();

    setLocalUrl( m_localUrl );

    PodcastChannel *channel = dynamic_cast<PodcastChannel *>( m_parent );
    if( channel && channel->autotransfer() && MediaBrowser::isAvailable() )
    {
        addToMediaDevice();
        MediaBrowser::queue()->URLsAdded();
    }

    updatePixmap();
}
void
PodcastEpisode::setLocalUrl( const KUrl &localUrl )
{
    m_localUrl = localUrl;
    m_bundle.setLocalURL( m_localUrl );
    CollectionDB::instance()->updatePodcastEpisode( dBId(), m_bundle );
    isOnDisk();
}

void
PodcastEpisode::addToMediaDevice()
{
    MetaBundle *bundle = new MetaBundle( localUrl() );
    PodcastChannel *channel = dynamic_cast<PodcastChannel *>( m_parent );
    if(channel && !channel->title().isEmpty())
        bundle->setAlbum(channel->title());
    if(!title().isEmpty())
        bundle->setTitle(title());

    MediaBrowser::queue()->addUrl( localUrl(), bundle );
}

void
PodcastEpisode::setLocalUrlBase( const QString &s )
{
    QString filename = m_localUrl.fileName();
    QString newL = s + filename;
    m_localUrl = KUrl( newL );
}

void
PodcastEpisode::setNew( const bool &n )
{
    if( n == isNew() ) return;

    m_bundle.setNew( n );
    updatePixmap();
    CollectionDB::instance()->updatePodcastEpisode( dBId(), m_bundle );

    // if we mark an item as listened, we might need to update the parent
    if( n == true )
        static_cast<PodcastChannel*>(m_parent)->setNew( true );
    else
        static_cast<PodcastChannel*>(m_parent)->checkAndSetNew();
}

void
PodcastEpisode::startAnimation()
{
    if( !m_animationTimer.isActive() )
        m_animationTimer.start( ANIMATION_INTERVAL );
}

void
PodcastEpisode::stopAnimation()
{
    m_animationTimer.stop();
    updatePixmap();
}

void
PodcastEpisode::slotAnimation()
{
    m_iconCounter % 2 ?
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast") ) ):
        setPixmap( 0, SmallIcon( Amarok::icon( "podcast2") ) );

    m_iconCounter++;
}

void
PodcastEpisode::setup()
{
    QFontMetrics fm( listView()->font() );
    int margin = listView()->itemMargin()*2;
    int h = fm.lineSpacing();
    if ( h % 2 > 0 ) h++;
    setHeight( h + margin );
}

void
PodcastEpisode::paintCell( QPainter *p, const QColorGroup &cg, int column, int width, int align )
{
    //flicker-free drawing
    static QPixmap buffer;
    buffer.resize( width, height() );

    if( buffer.isNull() )
    {
        K3ListViewItem::paintCell( p, cg, column, width, align );
        return;
    }

    QPainter pBuf( &buffer );
    // use alternate background
    pBuf.fillRect( buffer.rect(), isSelected() ? cg.highlight() : backgroundColor(0) );

    K3ListView *lv = static_cast<K3ListView *>( listView() );

    QFont font( p->font() );
    QFontMetrics fm( p->fontMetrics() );

    int text_x = 0;// lv->treeStepSize() + 3;
    int textHeight;

    textHeight = height();

    pBuf.setPen( isSelected() ? cg.highlightedText() : cg.text() );

    if( pixmap( column ) )
    {
        int y = (textHeight - pixmap(column)->height())/2;
        pBuf.drawPixmap( text_x, y, *pixmap(column) );
        text_x += pixmap(column)->width()+4;
    }

    pBuf.setFont( font );
    QFontMetrics fmName( font );

    QString name = text(column);
    const int _width = width - text_x - lv->itemMargin()*2;
    if( fmName.width( name ) > _width )
    {
        //decapitateString removes the channels title from the epsiodes title
        name = Amarok::decapitateString( name, static_cast<PodcastChannel *>(m_parent)->title() );
        name = fmName.elidedText( name, Qt::ElideRight, _width ); // use ... at the end if the text is longer that _width
    }

    pBuf.drawText( text_x, 0, width - text_x, textHeight, Qt::AlignVCenter, name );

    pBuf.end();
    p->drawPixmap( 0, 0, buffer );
}

void
PodcastEpisode::updateInfo()
{
    const QString body = "<tr><td><b>%1</b></td><td>%2</td></tr>";

    QString str  = "<html><body><table width=\"100%\" border=\"0\">";

    //str += body.arg( i18n( "Title" ),       m_bundle.title() );
    str += body.arg( i18n( "Description" ), m_bundle.description() );
    str += body.arg( i18n( "Date" ),        m_bundle.date() );
    str += body.arg( i18n( "Author" ),      m_bundle.author() );
    str += body.arg( i18n( "Type" ),        m_bundle.type() );
    str += body.arg( i18n( "URL" ),         m_bundle.url().prettyUrl() );
    str += body.arg( i18n( "Local URL" ),   isOnDisk() ? localUrl().prettyUrl() : i18n( "n/a" ) );
    str += "</table></body></html>";

    PlaylistBrowser::instance()->setInfo( text(0), str );
}


void
PodcastEpisode::slotDoubleClicked()
{
    KUrl::List list;

    isOnDisk() ?
        list.append( localUrl() ):
        list.append( url()      );

    The::playlistModel()->insertMedia( list, PlaylistNS::AppendAndPlay );
    setListened();
}


void
PodcastEpisode::showContextMenu( const QPoint &position )
{
    Q3PopupMenu menu( listView() );

    enum AMAROK_Actions { ACTIONS_LOAD, ACTIONS_APPEND, ACTIONS_QUEUE, ACTIONS_GET, 
        ACTIONS_ASSOCIATE, ACTIONS_DELETE, ACTIONS_MEDIA_DEVICE, ACTIONS_LISTENED, 
        ACTIONS_NEW, ACTIONS_OPEN_WITH /* has to be last */ };
    menu.insertItem( KIcon( Amarok::icon( "files" ) ), i18n( "&Load" ), ACTIONS_LOAD );
    menu.insertItem( KIcon( Amarok::icon( "add_playlist" ) ), i18n( "&Append to Playlist" ), ACTIONS_APPEND );
    menu.insertItem( KIcon( Amarok::icon( "queue_track" ) ), i18n( "&Queue Track" ), ACTIONS_QUEUE );

    int accuracy = 0;
    KMimeType::Ptr mimetype;
    if( isOnDisk() )
        mimetype = KMimeType::findByFileContent( localUrl().path(), &accuracy );
    if( accuracy <= 0 )
        mimetype = KMimeType::findByUrl( url() );
    KService::List offers = KMimeTypeTrader::self()->query( mimetype->name(), "Type == 'Application'" );
    if( offers.empty() || (offers.size()==1 && offers.first()->name()=="Amarok") )
    {
        menu.insertItem( KIcon( Amarok::icon( "run" ) ), i18n( "&Open With..."), ACTIONS_OPEN_WITH );
    }
    else
    {
        int i = 1;
        KMenu *openMenu = new KMenu;
        for( KService::List::iterator it = offers.begin();
                it != offers.end();
                ++it )
        {
            if( (*it)->name() != "Amarok" )
                openMenu->insertItem( KIcon( (*it)->icon() ), (*it)->name(), ACTIONS_OPEN_WITH+i );
            ++i;
        }
        openMenu->addSeparator();
        openMenu->insertItem( KIcon( Amarok::icon( "run" ) ), i18n( "&Other..."), ACTIONS_OPEN_WITH );
        menu.insertItem( KIcon( Amarok::icon( "run" ) ), i18n("&Open With"), openMenu, ACTIONS_OPEN_WITH );
    }

    if( MediaBrowser::isAvailable() )
    {
        menu.addSeparator();
        menu.insertItem( KIcon( Amarok::icon( "device" ) ),
                            i18n( "&Transfer to Media Device" ), ACTIONS_MEDIA_DEVICE );
        menu.setItemEnabled( ACTIONS_MEDIA_DEVICE, isOnDisk() );
    }

    menu.addSeparator();
    menu.insertItem( KIcon( Amarok::icon( "download" ) ), i18n( "&Download Media" ), ACTIONS_GET );
    menu.insertItem( KIcon( Amarok::icon( "attach" ) ), i18n( "&Associate with Local File" ), ACTIONS_ASSOCIATE );
    menu.insertItem( KIcon( Amarok::icon( "artist" ) ),   i18n( "Mark as &Listened" ),  ACTIONS_LISTENED );
    menu.insertItem( KIcon( Amarok::icon( "artist" ) ),   i18n( "Mark as &New" ),  ACTIONS_NEW );
    menu.insertItem( KIcon( Amarok::icon("remove") ), i18n( "De&lete Downloaded Podcast" ), ACTIONS_DELETE );

    menu.setItemEnabled( ACTIONS_GET, !isOnDisk() );
    menu.setItemEnabled( ACTIONS_ASSOCIATE, !isOnDisk() );
    menu.setItemEnabled( ACTIONS_DELETE, isOnDisk() );
    menu.setItemVisible( ACTIONS_LISTENED, isNew() );
    menu.setItemVisible( ACTIONS_NEW, !isNew() );

    uint id = menu.exec( position );
    switch( id )
    {
        case ACTIONS_LOAD:
            The::playlistModel()->clear();
            Playlist::instance()->setPlaylistName( text(0) );
            //FALL THROUGH
        case ACTIONS_APPEND:
            PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Append );
            break;

        case ACTIONS_QUEUE:
            PlaylistBrowser::instance()->addSelectedToPlaylist( Playlist::Queue );
            break;

         case ACTIONS_GET:
            PlaylistBrowser::instance()->downloadSelectedPodcasts();
            break;

        case ACTIONS_ASSOCIATE:
            associateWithLocalFile();
            break;

        case ACTIONS_DELETE:
            PlaylistBrowser::instance()->deleteSelectedPodcastItems();
            break;

        case ACTIONS_LISTENED:
            for ( Q3ListViewItemIterator it( listView(), Q3ListViewItemIterator::Selected); *it; ++it )
            {
                if ( isPodcastEpisode( *it ) )
                    static_cast<PodcastEpisode*>(*it)->setListened();
            }
            break;

        case ACTIONS_NEW:
            for ( Q3ListViewItemIterator it( listView(), Q3ListViewItemIterator::Selected); *it; ++it )
            {
                if ( isPodcastEpisode( *it ) )
                    static_cast<PodcastEpisode*>(*it)->setListened(false);
            }
            break;

        case ACTIONS_MEDIA_DEVICE:
            // tags on podcasts are sometimes bad, thus use other meta information if available
            if( isSelected() )
            {
                for ( Q3ListViewItemIterator it( listView(), Q3ListViewItemIterator::Selected); *it;  ++it)
                {
                    if( isPodcastEpisode( *it ) )
                    {
                        PodcastEpisode *podcast = static_cast<PodcastEpisode*>(*it);
                        if( podcast->isOnDisk() )
                            podcast->addToMediaDevice();
                    }
                }
            }
            else
                addToMediaDevice();

            MediaBrowser::queue()->URLsAdded();
            break;
        case ACTIONS_OPEN_WITH:
                {
                    KUrl::List urlList;
                    urlList.append( isOnDisk() ? localUrl() : url() );
                    KRun::displayOpenWithDialog( urlList, 0 );
                }
                break;

        default:
            if( id >= ACTIONS_OPEN_WITH+1 && id <= ACTIONS_OPEN_WITH + offers.size() )
            {
                KService::List::iterator it = offers.begin();
                for(uint i = ACTIONS_OPEN_WITH+1; i < id && i < ACTIONS_OPEN_WITH+offers.size(); ++i )
                {
                    ++it;
                }
                KService::Ptr ptr = offers.first();
                KUrl::List urlList;
                urlList.append( isOnDisk() ? localUrl() : url() );
                if( it != offers.end() )
                {
                    KRun::run(**it, urlList, 0);
                }
            }
            break;
    }
}


class AssociatePodcastDialog : public KDialog
{
    KUrlRequester *m_urlRequester;

    public:
    AssociatePodcastDialog( PodcastEpisode *item )
        : KDialog( Amarok::mainWindow() )
    {
        setCaption( i18n("Select Local File for %1",item->title()) );
        setModal( true );
        setButtons( Ok | Cancel );
        setDefaultButton( Ok );
        showButtonSeparator( false );


        KVBox *vbox = new KVBox( this );
        setMainWidget( vbox );

        vbox->setSpacing( KDialog::spacingHint() );

        m_urlRequester = new KUrlRequester( vbox );
        if( dynamic_cast<PodcastChannel *>(item->parent()) )
        m_urlRequester->setUrl( static_cast<PodcastChannel *>(item->parent())->saveLocation() );
    }
    KUrl url() const { return m_urlRequester->url(); }
};

void
PodcastEpisode::associateWithLocalFile()
{
    AssociatePodcastDialog d( this );
    if( d.exec() == KDialog::Accepted )
    {
        if( !d.url().isLocalFile() || !QFileInfo( d.url().path() ).isFile() )
            Amarok::StatusBar::instance()->shortMessage( i18n( "Invalid local podcast URL." ) );
        else
            setLocalUrl( d.url() );
    }
}


/////////////////////////////////////////////////////////////////////////////
///    CLASS SmartPlaylist
////////////////////////////////////////////////////////////////////////////

SmartPlaylist::SmartPlaylist( Q3ListViewItem *parent, Q3ListViewItem *after, const QString &name, const QString &query )
        : PlaylistBrowserEntry( parent, after, name )
        , m_sqlForTags( query )
        , m_title( name )
        , m_dynamic( false )
{
    setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );
    setDragEnabled( query.isEmpty() ? false : true );

    setText( 0, name );
}

SmartPlaylist::SmartPlaylist( Q3ListViewItem *parent, Q3ListViewItem *after, const QString &name, const QString &urls, const QString &tags )
        : PlaylistBrowserEntry( parent, after, name )
        , m_sqlForTags( tags )
        , m_title( name )
        , m_dynamic( false )
{
    setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );
    setDragEnabled( !urls.isEmpty() && !tags.isEmpty() );

    setText( 0, name );
}


SmartPlaylist::SmartPlaylist( Q3ListViewItem *parent, Q3ListViewItem *after, const QDomElement &xmlDefinition )
        : PlaylistBrowserEntry( parent, after )
        , m_after( after )
        , m_dynamic( false )
{
    setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );
    setXml( xmlDefinition );
    setDragEnabled( true );
}

int SmartPlaylist::length()
{
  QString sql = query();
  sql.replace(QRegExp("SELECT.*FROM"), "SELECT COUNT(*) FROM");
  CollectionDB *db = CollectionDB::instance();
  QStringList result = db->query( sql );

  if (! result.isEmpty())
    return result.first().toInt();
  else return 0;
}

void SmartPlaylist::setXml( const QDomElement &xml )
{
    m_xml = xml;
    m_title = xml.attribute( "name" );
    setText( 0, m_title );
    // ignore query, we now compute it when needed
    //m_sqlForTags = xml.namedItem( "sqlquery" ).toElement().text();
    m_sqlForTags = "";
    static QStringList genres;
    static QStringList artists;
    static QStringList composers;
    static QStringList albums;
    static QStringList years;
    static QStringList labels;

    //Delete all children before
    while( firstChild() )
        delete firstChild();

    QDomNode expandN = xml.namedItem( "expandby" );
    if ( !expandN.isNull() ) {
        // precompute query
        QString queryChildren = xmlToQuery( m_xml, true );

        QDomElement expand = expandN.toElement();
        QString field = expand.attribute( "field" );
        SmartPlaylist *item = this;
        if ( field == i18n("Genre") ) {
            if ( genres.isEmpty() ) {
                genres = CollectionDB::instance()->genreList();
            }
            oldForeach( genres ) {
                m_after = new SmartPlaylist( item, m_after, i18n( "%1", (*it) ),
                                             QString(queryChildren).replace(
                                                 "(*ExpandString*)", *it)  );
            }
        }
        if ( field == i18n("Artist") ) {
            if ( artists.isEmpty() ) {
                artists = CollectionDB::instance()->artistList();
            }
            oldForeach( artists ) {
                m_after = new SmartPlaylist( item, m_after, i18n( "By %1", (*it) ),
                                             QString(queryChildren).replace(
                                                 "(*ExpandString*)", *it)  );
            }
        }
        if ( field == i18n("Composer") ) {
            if ( composers.isEmpty() ) {
                composers = CollectionDB::instance()->composerList();
            }
            oldForeach( composers ) {
                m_after = new SmartPlaylist( item, m_after, i18n( "By %1", *it ),
                                             QString(queryChildren).replace(
                                                 "(*ExpandString*)", *it)  );
            }
        }
        if ( field == i18n("Album") ) {
            if ( albums.isEmpty() ) {
                albums = CollectionDB::instance()->albumList();
            }
            oldForeach( albums ) {
                m_after = new SmartPlaylist( item, m_after, i18n( "%1", *it ),
                                             QString(queryChildren).replace(
                                                 "(*ExpandString*)", *it)  );
            }
        }
        if ( field == i18n("Year") ) {
            if ( years.isEmpty() ) {
                years = CollectionDB::instance()->yearList();
            }
            oldForeach( years ) {
                m_after = new SmartPlaylist( item, m_after, i18n( "%1", *it ),
                                             QString(queryChildren).replace(
                                                 "(*ExpandString*)", *it)  );
            }
        }
        if ( field == i18n("Label") ) {
            if (labels.isEmpty() ) {
                labels = CollectionDB::instance()->labelList();
            }
            oldForeach( labels ) {
                m_after = new SmartPlaylist( item, m_after, i18n( "%1", *it ), QString(queryChildren).replace("(*ExpandString*)", *it)  );
            }
        }
    }

}

QString SmartPlaylist::query()
{
    if ( m_sqlForTags.isEmpty() ) m_sqlForTags = xmlToQuery( m_xml );
    return  m_sqlForTags
           .replace( "(*CurrentTimeT*)" ,
                     QString::number(QDateTime::currentDateTime().toTime_t()) )
           .replace( "(*ListOfFields*)" , QueryBuilder::dragSQLFields() )
           .replace( "(*MountedDeviceSelection*)" ,
                     CollectionDB::instance()->deviceidSelection() );
}

// static
QString
SmartPlaylist::xmlToQuery(const QDomElement &xml, bool forExpand /* = false */) {
    QueryBuilder qb;

    qb.initSQLDrag();
    // This code is partly copied from SmartPlaylistEditor -- but refactoring
    // to have it common would involve adding an internal data structure for smart
    // playlist queries. I think having the XML be that data structure is almost as good,
    // it's just a little more verbose when iterating.

    // Add filters
    QDomNodeList matchesList = xml.elementsByTagName( "matches" );
    for ( uint i = 0; i < matchesList.count(); i++ ) {
        QDomElement matches = matchesList.item( i ).toElement();
        QDomNodeList criteriaList = matches.elementsByTagName( "criteria" );

        if ( matches.attribute( "glue" ) == "OR" )
            qb.beginOR();
        else
            qb.beginAND();

        for ( uint j = 0; j < criteriaList.count(); j++ ) {
            QDomElement criteria = criteriaList.item( j ).toElement();
            QString field = criteria.attribute( "field" );
            int table;
            qint64 value;
            if ( !qb.getField( field, &table, &value ) ) continue;

            QStringList filters;
            // name conflict :) XML "value" -> QueryBuilder "filter"
            QDomNodeList domFilterList = criteria.elementsByTagName( "value" );
            for ( uint k = 0 ; k < domFilterList.count(); k++ ) {
                filters << domFilterList.item(k).toElement().text();
            }

            QString condition = criteria.attribute( "condition" );

            // Interpret dates
            bool isDate = (value & (QueryBuilder::valCreateDate
                                    | QueryBuilder::valAccessDate)) > 0;

            if ( isDate ) {
                QDateTime dt1, dt2;
                if ( condition == i18n( "is in the last" )
                     || condition == i18n( "is not in the last" ) ) {
                         QString period = criteria.attribute( "period" );
                         uint time = filters[0].toInt();
                         if ( period == "days" ) time *= 86400;
                         else if ( period == "months" ) time *= 86400 * 30;
                         else if ( period == "years" ) time *= 86400 * 365;
                         filters[0] = "(*CurrentTimeT*) - "  + QString::number( time );
                         if ( filters.count() == 1 ) filters.push_back( "" );
                         filters[1] = "(*CurrentTimeT*)";

                     }
                else {
                    dt1.setTime_t( filters[0].toInt() );
                    // truncate to midnight
                    if ( condition == i18n( "is after" ) )
                        dt1.setTime( QTime().addSecs(-1) );  // 11:59:59 pm
                    else
                        dt1.setTime( QTime() );
                    if ( filters.count() > 1 ) {
                        dt2.setTime_t( filters[1].toInt() );
                        // this is a "between", so always go till right before midnight
                        dt2.setTime( QTime().addSecs( -1 ) );
                    }
                }
            }

            if ( value & QueryBuilder::valLength ) {
                QString period = criteria.attribute( "period" );
                uint time1 = filters[0].toInt();
                if ( period == "minutes" )
                    time1 *= 60;
                else if ( period == "hours" )
                    time1 *= 3600;
                filters[0] = QString::number( time1 );
                if ( condition == i18n( "is between" ) )
                {
                    uint time2 = filters[1].toInt();
                    if ( period == "minutes" )
                        time2 *= 60;
                    else if ( period == "hours" )
                        time2 *= 3600;
                    filters[1] = QString::number( time2 );
                }
            }


            if ( condition == i18n( "contains" ) )
                qb.addFilter( table, value, filters[0] );
            else if ( condition == i18n( "does not contain" ) )
                qb.excludeFilter( table, value, filters[0]) ;
            else if ( condition == i18n( "is") )
                qb.addFilter( table, value, filters[0], QueryBuilder::modeNormal, true);
            else if ( condition == i18n( "is not" ) )
                qb.excludeFilter( table, value, filters[0], QueryBuilder::modeNormal,
                                  true);
            else if ( condition == i18n( "starts with" ) )
            {
                // need to take care of absolute paths
                if ( field == "tags.url" )
                    if ( filters[0].startsWith( '/' ) )
                        filters[0].prepend( '.' );
                    else if ( !filters[0].startsWith( "./" ) )
                        filters[0].prepend( "./" );
                qb.addFilter( table, value, filters[0], QueryBuilder::modeBeginMatch );
            }
            else if ( condition == i18n( "does not start with" ) )
            {
                // need to take care of absolute paths
                if ( field == "tags.url" )
                    if ( filters[0].startsWith( '/' ) )
                        filters[0].prepend( '.' );
                    else if ( !filters[0].startsWith( "./" ) )
                        filters[0].prepend( "./" );
                qb.excludeFilter( table, value, filters[0], QueryBuilder::modeBeginMatch );
            }
            else if ( condition == i18n( "ends with" ) )
                qb.addFilter( table, value, filters[0], QueryBuilder::modeEndMatch );
            else if ( condition == i18n( "does not end with" ) )
                qb.excludeFilter( table, value, filters[0], QueryBuilder::modeEndMatch );
            else if ( condition == i18n( "is greater than") || condition == i18n( "is after" ) )
                qb.addNumericFilter( table, value, filters[0], QueryBuilder::modeGreater );
            else if ( condition == i18n( "is smaller than") || condition == i18n( "is before" ) )
                qb.addNumericFilter( table, value, filters[0], QueryBuilder::modeLess );
            else if ( condition == i18n( "is between" )
                      || condition == i18n( "is in the last" ) )
                qb.addNumericFilter( table, value, filters[0], QueryBuilder::modeBetween,
                                     filters[1] );
            else if ( condition == i18n( "is not between" )
                      || condition == i18n( "is not in the last" ) )
                qb.addNumericFilter( table, value, filters[0],
                                     QueryBuilder::modeNotBetween, filters[1] );
        }

        if ( matches.attribute( "glue" ) == "OR" )
            qb.endOR();
        else
            qb.endAND();

    }

    // order by
    QDomNodeList orderbyList =  xml.elementsByTagName( "orderby" );
    for ( uint i = 0; i < orderbyList.count(); i++ ) {
        QDomElement orderby = orderbyList.item( i ).toElement();
        QString field = orderby.attribute( "field" );
        if ( field == "random" )
        {
            // shuffle
            if ( orderby.attribute("order" ) == "weighted" )
                qb.shuffle( QueryBuilder::tabStats, QueryBuilder::valScore );
            else if ( orderby.attribute("order" ) == "ratingweighted" )
                qb.shuffle( QueryBuilder::tabStats, QueryBuilder::valRating );
            else
                qb.shuffle();
        } else {
            // normal sort
            int table;
            qint64 value;
            if ( !qb.getField( field, &table, &value ) ) continue;
            qb.sortBy( table, value, orderby.attribute( "order" ) == "DESC" );
        }
    }

    if ( xml.hasAttribute( "maxresults" ) )
        qb.setLimit(0, xml.attribute( "maxresults" ).toInt() );

    // expand by, if needed
    if ( forExpand ) {
        // TODO: The most efficient way would be to pass the children the XML
        // and what to expand by, then have the children compute the query as needed.
        // This could save a few megs of RAM for queries, but this patch is getting
        // too big already, right now. Ovy
        QDomNodeList expandbyList = xml.elementsByTagName( "expandby" );
        for ( uint i = 0; i < expandbyList.count(); i++ ) {
            QDomElement expandby = expandbyList.item( i ).toElement();
            QString field = expandby.attribute( "field" );
            int table = QueryBuilder::tabGenre;  // make compiler happy
            if ( field == i18n( "Genre" ) )
                table = QueryBuilder::tabGenre;
            else if ( field == i18n( "Artist" ) )
                table = QueryBuilder::tabArtist;
            else if ( field == i18n( "Composer" ) )
                table = QueryBuilder::tabComposer;
            else if ( field == i18n( "Album" ) )
                table = QueryBuilder::tabAlbum;
            else if ( field == i18n( "Year" ) )
                table = QueryBuilder::tabYear;
            else if ( field == i18n( "Label" ) )
                table = QueryBuilder::tabLabels;

            qb.addFilter( table, QueryBuilder::valName,
                          "(*ExpandString*)",
                          QueryBuilder::modeNormal, true);
        }
    }

    return qb.query( true );
}


void SmartPlaylist::setDynamic( bool enable )
{
    enable ?
        setPixmap( 0, SmallIcon( "favorites" ) ) :
        setPixmap( 0, SmallIcon( Amarok::icon( "playlist" ) ) );
    m_dynamic = enable;
}

bool SmartPlaylist::isTimeOrdered()
{
    // matches statistics.createdate (firstplayed) and tags.createdate (modified date)
    QRegExp createDate( "ORDER BY.*createdate" );
    // matches last played
    QRegExp accessDate( "ORDER BY.*accessdate" );

    const QString sql = query();

    return ! ( ( sql.find( createDate, false ) == -1 ) /*not create ordered*/ &&
               ( sql.find( accessDate, false ) == -1 ) /*not access ordered*/ );
}

void SmartPlaylist::slotDoubleClicked()
{
    if( !query().isEmpty() )
    {
        Playlist::instance()->proposePlaylistName( text(0) );
        Playlist::instance()->insertMediaSql( query(), PlaylistNS::AppendAndPlay );
    }
}

void SmartPlaylist::showContextMenu( const QPoint &position )
{
    Q3PopupMenu menu( listView() );

    enum Actions { LOAD, ADD, QUEUE, EDIT, REMOVE, MEDIADEVICE_COPY, MEDIADEVICE_SYNC };

    menu.insertItem( KIcon( Amarok::icon( "files" ) ), i18n( "&Load" ), LOAD );
    menu.insertItem( KIcon( Amarok::icon( "add_playlist" ) ), i18n( "&Append to Playlist" ), ADD );
    menu.insertItem( KIcon( Amarok::icon( "queue_track" ) ), i18n( "&Queue Tracks" ), QUEUE );
    if( MediaBrowser::isAvailable() )
    {
        menu.addSeparator();
        menu.insertItem( KIcon( Amarok::icon( "device" ) ),
                i18n( "&Transfer to Media Device" ), MEDIADEVICE_COPY );
        menu.insertItem( KIcon( Amarok::icon( "device" ) ),
                i18n( "&Synchronize to Media Device" ), MEDIADEVICE_SYNC );
    }

    // Forbid removal of Collection
    if( isKept() )
    {
        menu.addSeparator();
        if ( isEditable() )
            menu.insertItem( KIcon( Amarok::icon("edit") ), i18n( "E&dit..." ), EDIT );
        menu.insertItem( KIcon( Amarok::icon("remove_from_playlist") ), i18n( "&Delete" ), REMOVE );
    }

    switch( menu.exec( position ) )
    {
        case LOAD:
            The::playlistModel()->clear();
            Playlist::instance()->setPlaylistName( text(0) );
            //FALL THROUGH
        case ADD:
            Playlist::instance()->insertMediaSql( query(), Playlist::Append );
            break;
        case QUEUE:
            Playlist::instance()->insertMediaSql( query(), Playlist::Queue );
            break;
        case EDIT:
            PlaylistBrowser::instance()->editSmartPlaylist( this );
            PlaylistBrowser::instance()->saveSmartPlaylists();
            break;
        case REMOVE:
            PlaylistBrowser::instance()->removeSelectedItems();
            break;
        case MEDIADEVICE_COPY:
            {
                const QString playlist = text(0);
                const QStringList values = CollectionDB::instance()->query( query() );
                MediaBrowser::queue()->addUrls( CollectionDB::instance()->URLsFromSqlDrag( values ), playlist );
            }
            break;
        case MEDIADEVICE_SYNC:
            MediaBrowser::queue()->syncPlaylist( text(0), query() );
            break;
    }
}

void SmartPlaylist::slotPostRenameItem( const QString newName )
{
    xml().setAttribute( "name", newName );
}

ShoutcastBrowser::ShoutcastBrowser( PlaylistCategory *parent )
    : PlaylistCategory( parent, 0, i18n( "Shoutcast Streams" ) )
    , m_downloading( false )
    , m_cj( 0 )
    , m_loading1( new QPixmap( KStandardDirs::locate("data", "amarok/images/loading1.png" ) ) )
    , m_loading2( new QPixmap( KStandardDirs::locate("data", "amarok/images/loading2.png" ) ) )
{
    setExpandable( true );
    setKept( false );
}

void ShoutcastBrowser::slotDoubleClicked()
{
    setOpen( !isOpen() );
}

void ShoutcastBrowser::setOpen( bool open )
{
    if( open == isOpen())
        return;

    if ( firstChild() ) // don't redownload everything
    {
        Q3ListViewItem::setOpen( open );
        return;
    }

    if( !m_animationTimer.isActive() )
        m_animationTimer.start( ANIMATION_INTERVAL );
    connect( &m_animationTimer, SIGNAL(timeout()), this, SLOT(slotAnimation()) );

    QStringList tmpdirs = KGlobal::dirs()->resourceDirs( "tmp" );
    QString tmpfile = tmpdirs[0];
    tmpfile += "/amarok-genres-" + KRandom::randomString(10) + ".xml-";

    //get the genre list
    if ( !m_downloading )
    {
        m_downloading = true;
        m_cj = KIO::copy( KUrl("http://www.shoutcast.com/sbin/newxml.phtml"), KUrl(tmpfile), false );
        connect( m_cj, SIGNAL( copyingDone( KIO::Job*, const KUrl&, const KUrl&, time_t, bool, bool))
                , this, SLOT(doneGenreDownload(KIO::Job*, const KUrl&, const KUrl&, time_t, bool, bool )));
        connect( m_cj, SIGNAL( result( KJob* )), this, SLOT( jobFinished( KJob* )));
    }

    Q3ListViewItem::setOpen( open );
}

void ShoutcastBrowser::slotAnimation()
{
    static int s_iconCounter = 0;
    s_iconCounter % 2 ?
        setPixmap( 0, *m_loading1 ):
        setPixmap( 0, *m_loading2 );

    s_iconCounter++;
}

void ShoutcastBrowser::doneGenreDownload( KIO::Job *job, const KUrl &from, const KUrl &to, time_t mtime, bool directory, bool renamed )
{
    Q_UNUSED( job ); Q_UNUSED( from ); Q_UNUSED( directory ); Q_UNUSED( renamed ); Q_UNUSED( mtime );

    QDomDocument doc( "genres" );
    QFile file( to.path() );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        warning() << "Cannot open shoutcast genre xml" << endl;
        m_downloading = false;
        return;
    }
    if ( !doc.setContent( &file ) )
    {
        warning() << "Cannot set shoutcast genre xml" << endl;
        file.close();
        m_downloading = false;
        return;
    }

    file.close();

    KIO::del( to, false, false );

    // We use this list to filter out some obscure genres
    QStringList bannedGenres;
    bannedGenres << "alles" << "any" << "anything" << "autopilot" << "backup" << "bandas" << "beer";
    bannedGenres << "catholic" << "chr" << "das" << "domaca" << "everything" << "fire" << "her" << "hollands";
    bannedGenres << "http" << "just" << "lokale" << "middle" << "noticias" << "only" << "scanner" << "shqip";
    bannedGenres << "good" << "super" << "wusf" << "www" << "zabavna" << "zouk" << "whatever" << "varios";
    bannedGenres << "varius" << "video" << "opm" << "non" << "narodna" << "muzyka" << "muzica" << "muzika";
    bannedGenres << "musique" << "music" << "multi" << "online" << "mpb" << "musica" << "musik" << "manele";
    bannedGenres << "paranormal" << "todos" << "soca" << "the" << "toda" << "trova" << "italo";
    bannedGenres << "auto" << "alternativo" << "best" << "clasicos" << "der" << "desi" << "die" << "emisora";
    bannedGenres << "voor" << "post" << "playlist" << "ned" << "gramy" << "deportes" << "bhangra" << "exitos";
    bannedGenres << "doowop" << "radio" << "radyo" << "railroad" << "program" << "mostly" << "hot";
    bannedGenres << "deejay" << "cool" << "big" << "exitos" << "mp3" << "muzyczne" << "nederlandstalig";
    bannedGenres << "max" << "informaci" << "halk" << "dobra" << "welcome" << "genre";

    // This maps genres that should be combined together
    QMap<QString, QString> genreMapping;
    genreMapping["Romania"] = "Romanian";
    genreMapping["Turk"] = "Turkish";
    genreMapping["Turkce"] = "Turkish";
    genreMapping["Polskie"] = "Polska";
    genreMapping["Polski"] = "Polish";
    genreMapping["Greece"] = "Greek";
    genreMapping["Dnb"] = "Drum&bass";
    genreMapping["Classic"] = "Classical";
    genreMapping["Goth"] = "Gothic";
    genreMapping["Alt"] = "Alternative";
    genreMapping["Italiana"] = "Italian";
    genreMapping["Japan"] = "Japanese";
    genreMapping["Oldie"] = "Oldies";
    genreMapping["Nederlands"] = "Dutch";
    genreMapping["Variety"] = "Various";
    genreMapping["Soundtracks"] = "Soundtrack";
    genreMapping["Gaming"] = "Game";
    genreMapping["Sports"] = "Sport";
    genreMapping["Spain"] = "Spanish";

    QDomElement docElem = doc.documentElement();
    QDomNode n = docElem.firstChild();
    Q3ListViewItem *last = 0;
    QMap<QString, Q3ListViewItem *> genreCache; // maps names to the listview item
    while( !n.isNull() )
    {
        QDomElement e = n.toElement(); // try to convert the node to an element.
        const QString name = e.attribute( "name" );
        if( !name.isNull() && !bannedGenres.contains( name.toLower() ) && !genreMapping.contains( name ) )
        {
            last = new ShoutcastGenre( this, last, name );
            genreCache[ name ] = last; // so we can append genres later if needed
        }
        n = n.nextSibling();
    }
    // Process the mapped (alternate) genres
    for( QMap<QString, QString>::iterator it = genreMapping.begin(); it != genreMapping.end(); ++it )
    {
        // Find the target genre
        ShoutcastGenre *existingGenre = dynamic_cast<ShoutcastGenre *> ( genreCache[ it.data() ] );
        if( existingGenre != 0 )
            existingGenre->appendAlternateGenre( it.key() );
    }
    m_downloading = false;
    m_animationTimer.stop();
    setPixmap( 0, SmallIcon( Amarok::icon( "files" ) ) );
    setOpen( true );
}

void ShoutcastBrowser::jobFinished( KJob *job )
{
    m_downloading = false;
    m_animationTimer.stop();
    setPixmap( 0, SmallIcon( Amarok::icon( "files" ) ) );

    if ( job->error() )
        job->uiDelegate()->showErrorMessage();
}

ShoutcastGenre::ShoutcastGenre( ShoutcastBrowser *browser, Q3ListViewItem *after, QString genre )
    : PlaylistCategory( browser, after, genre )
    , m_downloading( false )
    , m_loading1( new QPixmap( KStandardDirs::locate("data", "amarok/images/loading1.png" ) ) )
    , m_loading2( new QPixmap( KStandardDirs::locate("data", "amarok/images/loading2.png" ) ) )
{
    setExpandable( true );
    setKept( false );
    m_genre = genre.replace( "&", "%26" ); //fix &
}

void ShoutcastGenre::slotDoubleClicked()
{
    setOpen( !isOpen() );
}

void ShoutcastGenre::setOpen( bool open )
{
    if( open == isOpen())
        return;

    if( firstChild() ) // don't redownload everything
    {
        Q3ListViewItem::setOpen( open );
        return;
    }

    if( !m_animationTimer.isActive() )
        m_animationTimer.start( ANIMATION_INTERVAL );
    connect( &m_animationTimer, SIGNAL(timeout()), this, SLOT(slotAnimation()) );

    QStringList tmpdirs = KGlobal::dirs()->resourceDirs( "tmp" );

    //get the genre list from shoutcast async, and when its done call the finish up functions to process
    if( !m_downloading)
    {
        m_downloading = true;
        m_totalJobs = 0;
        m_completedJobs = 0;
        startGenreDownload(  m_genre, tmpdirs[0] );
        for( QStringList::iterator it = m_alternateGenres.begin(); it != m_alternateGenres.end(); ++it )
            startGenreDownload( *it, tmpdirs[0] );
    }
}

void ShoutcastGenre::startGenreDownload( QString genre, QString tmppath )
{
    QString tmpfile = tmppath + "/amarok-list-" + genre + '-' + KRandom::randomString(10) + ".xml";
    KIO::CopyJob *cj = KIO::copy( "http://www.shoutcast.com/sbin/newxml.phtml?genre=" + genre, tmpfile, false );
    connect( cj, SIGNAL( copyingDone     ( KIO::Job*, const KUrl&, const KUrl&, time_t, bool, bool ) ),
             this,   SLOT( doneListDownload( KIO::Job*, const KUrl&, const KUrl&, time_t, bool, bool ) ) );
    connect( cj, SIGNAL( result     ( KJob* ) ),
             this,   SLOT( jobFinished( KJob* ) ) );
    m_totalJobs++;
}

void ShoutcastGenre::slotAnimation()
{
    static int s_iconCounter = 0;
    s_iconCounter % 2 ?
        setPixmap( 0, *m_loading1 ):
        setPixmap( 0, *m_loading2 );

    s_iconCounter++;
}

void ShoutcastGenre::doneListDownload( KIO::Job *job, const KUrl &from, const KUrl &to, time_t mtime, bool directory, bool renamed )
{
    Q_UNUSED( job ); Q_UNUSED( from ); Q_UNUSED( directory ); Q_UNUSED( renamed ); Q_UNUSED( mtime );

    m_completedJobs++;

    QDomDocument doc( "list" );
    QFile file( to.path() );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        warning() << "Cannot open shoutcast playlist xml" << endl;
        m_downloading = false;
        return;
    }
    if ( !doc.setContent( &file ) )
    {
        warning() << "Cannot set shoutcast playlist xml" << endl;
        file.close();
        m_downloading = false;
        return;
    }

    file.close();

    KIO::del(to, false, false);

    //Go through the XML file and add all the stations
    QDomElement docElem = doc.documentElement();
    QDomNode n = docElem.firstChild();
    while( !n.isNull() )
    {
        QDomElement e = n.toElement(); // try to convert the node to an element.
        if( e.hasAttribute( "name" ) )
        {
            if( !e.attribute( "name" ).isNull() && ! m_stations.contains( e.attribute( "name" ) ) )
            {
                m_stations << e.attribute( "name" );
                StreamEntry* entry = new StreamEntry( this, this,
                    "http://www.shoutcast.com/sbin/shoutcast-playlist.pls?rn="
                    + e.attribute( "id" ) + "&file=filename.pls", e.attribute( "name" ));

                entry->setKept( false );
            }
        }
        n = n.nextSibling();
    }
    if( m_completedJobs == m_totalJobs )
    {
        setOpen( true );
        m_downloading = false;
        m_animationTimer.stop();
        setPixmap( 0, SmallIcon( Amarok::icon( "files" ) ) );
    }
}

void ShoutcastGenre::jobFinished( KJob *job )
{
    m_downloading = false;
    m_animationTimer.stop();
    setPixmap( 0, SmallIcon( Amarok::icon( "files" ) ) );

    if( job->error() )
        job->uiDelegate()->showErrorMessage();
}

#include "playlistbrowseritem.moc"
