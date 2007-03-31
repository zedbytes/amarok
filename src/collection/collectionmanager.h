/*
 *  Copyright (c) 2007 Maximilian Kossick <maximilian.kossick@googlemail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#ifndef AMAROK_COLLECTIONMANAGER_H
#define AMAROK_COLLECTIONMANAGER_H

#include "collection.h"
#include "querymaker.h"

#include <QList>
#include <QObject>

class CollectionManager : public QObject
{
    Q_OBJECT

    public:
        static CollectionManager * instance();

        virtual ~CollectionManager();

        QueryMaker * queryMaker();
        void startFullScan();

        QList<Collection*> collections();

    private slots:
        void slotNewCollection( Collection *newCollection );

    private:
        CollectionManager();

        void init();

        class Private;
        Private * const d;
};


#endif /* AMAROK_COLLECTIONMANAGER_H */