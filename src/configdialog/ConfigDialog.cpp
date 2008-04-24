/***************************************************************************
 *   Copyright (C) 2004-2007 by Mark Kretschmann <markey@web.de>           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "ConfigDialog.h"

#include "Amarok.h"
#include "amarokconfig.h"
#include "debug.h"

#include "CollectionConfig.h"
#include "GeneralConfig.h"
#include "MediadeviceConfig.h"
#include "OsdConfig.h"
#include "PlaybackConfig.h"
#include "ServiceConfig.h"

#include <KLocale>

//////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC
//////////////////////////////////////////////////////////////////////////////////////////

Amarok2ConfigDialog::Amarok2ConfigDialog( QWidget *parent, const char* name, KConfigSkeleton *config )
   : KConfigDialog( parent, name, config )
{
    setAttribute( Qt::WA_DeleteOnClose );

    ConfigDialogBase* collection  = new CollectionConfig( this );
    ConfigDialogBase* general     = new GeneralConfig( this );
    ConfigDialogBase* mediadevice = new MediadeviceConfig( this );
    ConfigDialogBase* osd         = new OsdConfig( this );
    ConfigDialogBase* playback    = new PlaybackConfig( this );
    ConfigDialogBase* services    = new ServiceConfig( this );

    addPage( general,     i18nc( "Miscellaneous settings", "General" ), "preferences-other-amarok", i18n( "Configure General Options" ) );
    addPage( playback,    i18n( "Playback" ), "preferences-media-playback-amarok", i18n( "Configure Playback" ) );
    addPage( osd,         i18n( "On Screen Display" ), "preferences-indicator-amarok", i18n( "Configure On-Screen-Display" ) );
    addPage( collection,  i18n( "Collection" ), "collection-amarok", i18n( "Configure Collection" ) );
    addPage( services,    i18n( "Internet Services" ), "services-amarok", i18n( "Configure Services" ) );
    addPage( mediadevice, i18n( "Media Devices" ), "preferences-multimedia-player-amarok", i18n( "Configure Portable Player Support" ) );

    setButtons( Help | Ok | Apply | Cancel );
}

Amarok2ConfigDialog::~Amarok2ConfigDialog()
{
    DEBUG_FUNC_INFO
}


/** Reimplemented from KConfigDialog */
void Amarok2ConfigDialog::addPage( ConfigDialogBase *page, const QString &itemName, const QString &pixmapName, const QString &header, bool manage )
{
    connect( page, SIGNAL( settingsChanged( const QString& ) ), this, SIGNAL( settingsChanged( const QString& ) ) );

    // Add the widget pointer to our list, for later reference
    m_pageList << page;

    KConfigDialog::addPage( page, itemName, pixmapName, header, manage );
}

/** Show page by object name */
void Amarok2ConfigDialog::showPageByName( const QByteArray& page )
{
    for( int index = 0; index < m_pageList.count(); index++ ) {
        if ( m_pageList[index]->objectName() == page ) {
            KConfigDialog::setCurrentPage( qobject_cast<KPageWidgetItem*>( m_pageList[index] ) );
            return;
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
// PROTECTED SLOTS
//////////////////////////////////////////////////////////////////////////////////////////

/**
 * Update the settings from the dialog.
 * Example use: User clicks Ok or Apply button in a configure dialog.
 * REIMPLEMENTED
 */
void Amarok2ConfigDialog::updateSettings()
{
    foreach( ConfigDialogBase* page, m_pageList )
        page->updateSettings();
}


/**
 * Update the configuration-widgets in the dialog based on Amarok's current settings.
 * Example use: Initialisation of dialog.
 * Example use: User clicks Reset button in a configure dialog.
 * REIMPLEMENTED
 */
void Amarok2ConfigDialog::updateWidgets()
{
    foreach( ConfigDialogBase* page, m_pageList )
        page->updateWidgets();
}


/**
 * Update the configuration-widgets in the dialog based on the default values for Amarok's settings.
 * Example use: User clicks Defaults button in a configure dialog.
 * REIMPLEMENTED
 */
void Amarok2ConfigDialog::updateWidgetsDefault()
{
    foreach( ConfigDialogBase* page, m_pageList )
        page->updateWidgetsDefault();
}


//////////////////////////////////////////////////////////////////////////////////////////
// PROTECTED
//////////////////////////////////////////////////////////////////////////////////////////

/**
 * @return true if any configuration items we are managing changed from Amarok's stored settings
 * We manage the engine combo box and some of the OSD settings
 * REIMPLEMENTED
 */
bool Amarok2ConfigDialog::hasChanged()
{
    DEBUG_BLOCK

    bool changed = false;

    foreach( ConfigDialogBase* page, m_pageList )
        if( page->hasChanged() ) {
            changed = true;
            debug() << "Changed: " << page->metaObject()->className();
        }

    return changed;
}


/** REIMPLEMENTED */
bool Amarok2ConfigDialog::isDefault()
{
    bool def = false;

    foreach( ConfigDialogBase* page, m_pageList )
        if( page->hasChanged() )
            def = true;

    return def;
}


//////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE SLOTS
//////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE
//////////////////////////////////////////////////////////////////////////////////////////


#include "ConfigDialog.moc"
