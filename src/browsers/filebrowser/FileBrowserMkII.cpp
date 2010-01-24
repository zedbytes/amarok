/****************************************************************************************
 * Copyright (c) 2010 Nikolaj Hald Nielsen <nhn@kde.org>                                *
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

#include "FileBrowserMkII.h"

#include "Debug.h"
#include "EngineController.h"
#include "playlist/PlaylistController.h"
#include "widgets/PrettyTreeView.h"

#include <QDir>
#include <QFileSystemModel>

FileBrowserMkII::FileBrowserMkII( const char * name, QWidget *parent )
    : BrowserCategory( name, parent )
    , m_directoryLoader( 0 )
{
    m_fileSystemModel = new QFileSystemModel( this );
    m_fileSystemModel->setRootPath( QDir::rootPath() );

    Amarok::PrettyTreeView * treeView = new Amarok::PrettyTreeView( this );
    treeView->setSortingEnabled( true );
    treeView->setFrameStyle( QFrame::NoFrame );
    treeView->setModel( m_fileSystemModel );
    treeView->hideColumn( 1 );
    treeView->hideColumn( 2 );
    treeView->hideColumn( 3 );

    connect( treeView, SIGNAL( doubleClicked( const QModelIndex & ) ), this, SLOT( itemActivated( const QModelIndex & ) ) );
}


void FileBrowserMkII::itemActivated( const QModelIndex &index )
{
    DEBUG_BLOCK
    KUrl filePath = KUrl( m_fileSystemModel->filePath( index ) );

    debug() << "activated url: " << filePath.url();
    debug() << "filename: " << filePath.fileName();

    QList<KUrl> urls;
    urls << filePath;
    if( m_fileSystemModel->isDir( index ) ) {
        if( m_directoryLoader == 0 )
            m_directoryLoader = new DirectoryLoader();

        m_directoryLoader->insertAtRow( 99999999 ); //lazy way of saying last one...
        m_directoryLoader->init( urls );
    }
    else
    {
        if( EngineController::canDecode( filePath ) )
        {
            The::playlistController()->insertOptioned( urls, Playlist::AppendAndPlay );
        }

    }
    
}