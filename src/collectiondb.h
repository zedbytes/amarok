// (c) 2004 Mark Kretschmann <markey@web.de>
// (c) 2004 Christian Muehlhaeuser <chris@chris.de>
// See COPYING file for licensing information.

#ifndef AMAROK_COLLECTIONDB_H
#define AMAROK_COLLECTIONDB_H

#include <qobject.h>         //baseclass
#include <qstringlist.h>     //stack allocated
#include <qdir.h>            //stack allocated

class sqlite;
class ThreadWeaver;
class MetaBundle;

class CollectionDB : public QObject
{
    Q_OBJECT
    
    public:
        static const int COVER_SIZE = 100;
        
        CollectionDB();
        ~CollectionDB();

        bool isDbValid();
        bool isEmpty();
        QString albumSongCount( const QString artist_id, const QString album_id );

        QString getPathForAlbum( const uint artist_id, const uint album_id );
        QString getPathForAlbum( const QString artist, const QString album );

        QString getImageForAlbum( const uint artist_id, const uint album_id, const QString defaultImage, const uint width = COVER_SIZE );
        QString getImageForAlbum( const QString artist, const QString album, const QString defaultImage, const uint width = COVER_SIZE );
        
        QString getImageForPath( const QString path, const QString defaultImage, const uint width = COVER_SIZE );
        void addImageToPath( const QString path, const QString image, bool temporary );

        QStringList artistList();
        QStringList albumList();

        bool getMetaBundleForUrl( const QString url, MetaBundle *bundle );
        void incSongCounter( const QString url );
        void updateDirStats( QString path, const long datetime );
        void removeSongsInDir( QString path );
        bool isDirInCollection( QString path );
        bool isFileInCollection( const QString url );
        void removeDirFromCollection( QString path );

        /**
         * Executes an SQL statement on the already opened database
         * @param statement SQL program to execute. Only one SQL statement is allowed.
         * @retval values   will contain the queried data, set to NULL if not used
         * @retval names    will contain all column names, set to NULL if not used
         * @return          true if successful
         */
        bool execSql( const QString& statement, QStringList* const values = 0, QStringList* const names = 0, const bool debug = false );

        /**
         * Returns the rowid of the most recently inserted row
         * @return          int rowid
         */
        int sqlInsertID();
        QString escapeString( QString string );

        uint getValueID( QString name, QString value, bool autocreate = true, bool useTempTables = false );
        QString getValueFromID( QString table, uint id );
        void createTables( const bool temporary = false );
        void dropTables( const bool temporary = false );
        void moveTempTables();
        void createStatsTable();
        void dropStatsTable();

        void purgeDirCache();
        void scanModifiedDirs( bool recursively );
        void scan( const QStringList& folders, bool recursively );
        void updateTags( const QString &url, const MetaBundle &bundle );
        void updateTag( const QString &url, const QString &field, const QString &newTag );
        
        void retrieveFirstLevel( QString category1, QString category2, QString filter, QStringList* const values, QStringList* const names );
        void retrieveSecondLevel( QString itemText, QString category1, QString category2, QString filter, QStringList* const values, QStringList* const names );
        void retrieveThirdLevel( QString itemText1, QString itemText2, QString category1, QString category2, QString filter, QStringList* const values, QStringList* const names );

        void retrieveFirstLevelURLs( QString itemText, QString category1, QString category2, QString filter, QStringList* const values, QStringList* const names );
        void retrieveSecondLevelURLs( QString itemText1, QString itemText2, QString category1, QString category2, QString filter, QStringList* const values, QStringList* const names );

        QString m_amazonLicense;
    
    signals:
        void scanDone( bool changed );
        void coverFetched( const QString &key );
        void coverFetched();
        
    public slots:
        void fetchCover( QObject* parent, const QString& artist, const QString& album, bool noedit );
        void stopScan();
                
    private slots:
        void dirDirty( const QString& path );
        void saveCover( const QString& keyword, const QPixmap& image );

    private:
        void customEvent( QCustomEvent* );

        sqlite* m_db;
        ThreadWeaver* m_weaver;
        bool m_monitor;
        QDir m_cacheDir;
        QDir m_coverDir;
};


#endif /* AMAROK_COLLECTIONDB_H */
