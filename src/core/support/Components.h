/****************************************************************************************
 * Copyright (c) 2010 Maximilian Kossick <maximilian.kossick@googlemail.com>            *
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

#ifndef AMAROK_COMPONENTS_H
#define AMAROK_COMPONENTS_H

#include "core/amarokcore_export.h"

namespace Amarok
{
    class ApplicationController;
}

namespace Collections
{
    class CollectionLocationDelegate;
}

namespace Transcoding
{
    class Controller;
}

namespace StatSyncing
{
    class Controller;
}

class CollectionManager;
class EngineController;
class SqlStorage;

namespace Amarok
{
    namespace Components
    {
        AMAROKCORE_EXPORT CollectionManager *collectionManager();
        AMAROKCORE_EXPORT CollectionManager *setCollectionManager( CollectionManager *mgr );

        AMAROKCORE_EXPORT EngineController *engineController();
        AMAROKCORE_EXPORT EngineController *setEngineController( EngineController *controller );

        AMAROKCORE_EXPORT SqlStorage *sqlStorage();
        AMAROKCORE_EXPORT SqlStorage *setSqlStorage( SqlStorage *storage );

        AMAROKCORE_EXPORT Amarok::ApplicationController *applicationController();
        AMAROKCORE_EXPORT Amarok::ApplicationController *setApplicationController( Amarok::ApplicationController *controller );

        AMAROKCORE_EXPORT Collections::CollectionLocationDelegate *collectionLocationDelegate();
        AMAROKCORE_EXPORT Collections::CollectionLocationDelegate *setCollectionLocationDelegate( Collections::CollectionLocationDelegate *delegate );

        AMAROKCORE_EXPORT Transcoding::Controller *transcodingController();
        AMAROKCORE_EXPORT Transcoding::Controller *setTranscodingController( Transcoding::Controller *controller );

        AMAROKCORE_EXPORT StatSyncing::Controller *statSyncingController();
        AMAROKCORE_EXPORT StatSyncing::Controller *setStatSyncingController( StatSyncing::Controller *controller );
    }
}

#endif
