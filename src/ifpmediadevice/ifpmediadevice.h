/***************************************************************************
 * copyright            : (C) 2005 Seb Ruiz <me@sebruiz.net>               *
 *                                                                         *
 * With some code helpers from KIO_IFP                                     *
 *                        (c) 2004 Thomas Loeber <ifp@loeber1.de>          *
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef AMAROK_IFPMEDIADEVICE_H
#define AMAROK_IFPMEDIADEVICE_H

extern "C" {
    #include <ifp.h>
    #include <usb.h>
}

#include "mediabrowser.h"

#include <kurl.h>

// #include <qbitarray.h>
#include <qptrlist.h>

class IfpMediaItem;

class IfpMediaDevice : public MediaDevice
{
    Q_OBJECT

    public:
                          IfpMediaDevice( MediaDeviceView* parent, MediaDeviceList* listview );
        virtual           ~IfpMediaDevice();

        bool              isConnected() { return m_connected; }
        void              rmbPressed( MediaDeviceList *deviceList, QListViewItem* qitem, const QPoint& point, int );

    protected:
        bool              openDevice( bool silent=false );
        bool              closeDevice();

        void              lockDevice( bool ) {}
        void              unlockDevice() {}
        void              synchronizeDevice() {}

        MediaItem        *copyTrackToDevice( const MetaBundle& bundle, const PodcastInfo *info );
        int               deleteItemFromDevice( MediaItem *item, bool onlyPlayed = false );
        bool              getCapacity( unsigned long *total, unsigned long *available );
        MediaItem        *newDirectory( const QString &name, MediaItem *parent );
        void              addToDirectory( MediaItem *directory, QPtrList<MediaItem> items );

        void              addToPlaylist( MediaItem *, MediaItem *, QPtrList<MediaItem> ) {}
        MediaItem        *newPlaylist( const QString &, MediaItem *, QPtrList<MediaItem> ) { return 0; }
        
        void              cancelTransfer() {} // we don't have to do anything, we check m_cancelled

    protected slots:
        void              renameItem( QListViewItem *item );
        void              expandItem( QListViewItem *item );

    private:
        enum              Error { ERR_ACCESS_DENIED, ERR_CANNOT_RENAME, ERR_DISK_FULL, ERR_COULD_NOT_WRITE };

        // To expensive to implement on a non-database device
        MediaItem        *trackExists( const MetaBundle& ) { return 0; }

        bool              checkResult( int result, QString message );
        // Will iterate over parents and add directory name to the item.
        // getFilename = false will return only parent structure, as opposed to returning the filename as well
        QString           getFullPath( const QListViewItem *item, const bool getFilename = true );

        // upload
        int               uploadTrack( const QCString& src, const QCString& dest );
        static int        uploadCallback( void *pData, struct ifp_transfer_status *progress );

        // listDir
        void              listDir( const QString &dir );
        static int        listDirCallback( void *pData, int type, const char *name, int size );
        int               addTrackToList( int type, QString name, int size=0 );

        int               setProgressInfo( struct ifp_transfer_status *progress );

        // IFP device
        struct usb_device *m_dev;
        usb_dev_handle    *m_dh;
        struct ifp_device  m_ifpdev;

        bool               m_connected;

        IfpMediaItem      *m_last;
        //used to specify new IfpMediaItem parent. Make sure it is restored to 0 (m_listview)
        QListViewItem     *m_tmpParent;
};

#endif /*AMAROK_IFPMEDIADEVICE_H*/

