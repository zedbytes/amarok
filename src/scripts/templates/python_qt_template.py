#!/usr/bin/env python

############################################################################
# Python-Qt template script for amaroK
# (c) 2005 Mark Kretschmann <markey@web.de>
#
# Depends on: Python 2.2, PyQt
############################################################################
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
############################################################################

import ConfigParser
import os
import sys
import threading

try:
    from qt import *
except:
    popen( "kdialog --sorry 'PyQt (Qt bindings for Python) is required for this script.'" )
    raise


# Replace with real name
debug_prefix = "[Test Script]"


class ConfigDialog( QDialog ):
    """ Configuration widget """

    def __init__( self ):
        QDialog.__init__( self )
        self.setWFlags( Qt.WDestructiveClose )
        self.setCaption( "Test Script - amaroK" )

        foo = None
        try:
            config = ConfigParser.ConfigParser()
            config.read( "testrc" )
            foo = config.get( "General", "foo" )
        except:
            pass

        self.adjustSize()

    def save( self ):
        """ Saves configuration to file """

        self.file = file( "testrc", 'w' )

        self.config = ConfigParser.ConfigParser()
        self.config.add_section( "General" )
        self.config.set( "General", "foo", foovar )
        self.config.write( self.file )
        self.file.close()

        self.accept()


class Notification( QCustomEvent ):
    __super_init = QCustomEvent.__init__
    def __init__( self, str ):
        self.__super_init(QCustomEvent.User + 1)
        self.string = str

class Test( QApplication ):
    """ The main application, also sets up the Qt event loop """

    def __init__( self, args ):
        QApplication.__init__( self, args )
        debug( "Started." )

        # Start separate thread for reading data from stdin
        self.stdinReader = threading.Thread( target = self.readStdin )
        self.stdinReader.start()

        self.readSettings()

    def readSettings( self ):
        """ Reads settings from configuration file """

        try:
            foovar = config.get( "General", "foo" )

        except:
            debug( "No config file found, using defaults." )


############################################################################
# Stdin-Reader Thread
############################################################################

    def readStdin( self ):
        """ Reads incoming notifications from stdin """

        while True:
            # Read data from stdin. Will block until data arrives.
            line = sys.stdin.readline()

            if line:
                qApp.postEvent( self, Notification(line) )
            else:
                break


############################################################################
# Notification Handling
############################################################################

    def customEvent( self, notification ):
        """ Handles notifications """

        string = QString(notification.string)
        debug( "Received notification: " + str( string ) )

        if string.contains( "configure" ):
            self.configure()

        if string.contains( "engineStateChange: play" ):
            self.engineStatePlay()

        if string.contains( "engineStateChange: idle" ):
            self.engineStateIdle()

        if string.contains( "engineStateChange: pause" ):
            self.engineStatePause()

        if string.contains( "engineStateChange: empty" ):
            self.engineStatePause()

        if string.contains( "trackChange" ):
            self.trackChange()

# Notification callbacks. Implement these functions to react to specific notification
# events from amaroK:

    def configure( self ):
        debug( "configuration" )

        self.dia = ConfigDialog()
        self.dia.show()
        self.connect( self.dia, SIGNAL( "destroyed()" ), self.readSettings )

    def engineStatePlay( self ):
        """ Called when Engine state changes to Play """
        pass

    def engineStateIdle( self ):
        """ Called when Engine state changes to Idle """
        pass

    def engineStatePause( self ):
        """ Called when Engine state changes to Pause """
        pass

    def engineStateEmpty( self ):
        """ Called when Engine state changes to Empty """
        pass

    def trackChange( self ):
        """ Called when a new track starts """
        pass


############################################################################

def debug( message ):
    """ Prints debug message to stdout """

    print debug_prefix + " " + message

def main( args ):
    app = Test( args )

    app.exec_loop()

if __name__ == "__main__":
    main( sys.argv )

