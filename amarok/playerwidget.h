/***************************************************************************
                         playerwidget.h  -  description
                            -------------------
   begin                : Mit Nov 20 2002
   copyright            : (C) 2002 by Mark Kretschmann
   email                :
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include "amarokslider.h"

#include <qguardedptr.h>
#include <qlabel.h>
#include <qwidget.h>
#include <qpixmap.h>

#include <ksystemtray.h>
#include <khelpmenu.h> //inlined helpmenu()

class QBitmap;
class QBoxLayout;
class QFrame;
class QMouseEvent;
class QMoveEvent;
class QShowEvent;
class QPaintEvent;
class QPopupMenu;
class QPushButton;
class QString;
class QTimer;
class QTimerEvent;
class QToolButton;

class KActionCollection;
class KSystemTray;

class AmarokButton;
class AnalyzerBase;
class ArtsConfigWidget;

class PlayerApp;
extern PlayerApp *pApp;

class PlayerWidget;

class AmarokDcopHandler;

/**
 *@author mark
 */

// CLASS AmarokSystray ------------------------------------------------------------

class AmarokSystray : public KSystemTray
{
   public:
      AmarokSystray( PlayerWidget *, KActionCollection * );

   private:
      void mousePressEvent( QMouseEvent * );
      void wheelEvent( QWheelEvent * );
      void showEvent( QShowEvent * ) {} //Don't add me a Quit button automagically
};


// CLASS PlayerWidget ------------------------------------------------------------

class PlayerWidget : public QWidget
{
        Q_OBJECT

    public:
        PlayerWidget( QWidget *parent = 0, const char *name = 0 );
        ~PlayerWidget();

        void setScroll( QString = "", const QString & = "?", const QString & = "?" );
        void drawScroll();
        void timeDisplay( bool remaining, int hours, int minutes, int seconds );
        const KPopupMenu *helpMenu() const { return m_helpMenu->menu(); }

        void show();
        void hide();

        // ATTRIBUTES ------
        KActionCollection *m_pActionCollection;

        QPopupMenu *m_pPopupMenu;
        AnalyzerBase *m_pVis;
        QFrame *m_pFrame;
        QFrame *m_pFrameButtons;
        AmarokSlider *m_pSlider;
        AmarokSlider *m_pSliderVol;
        QLabel *m_pTimeDisplayLabel;
        
        AmarokButton *m_pButtonPl;
        AmarokButton *m_pButtonEq;
        AmarokButton *m_pButtonLogo;

        QPushButton *m_pButtonPrev;
        QPushButton *m_pButtonPlay;
        QPushButton *m_pButtonPause;
        QPushButton *m_pButtonStop;
        QPushButton *m_pButtonNext;

        QGuardedPtr<ArtsConfigWidget> m_pPlayObjConfigWidget;

        void wheelEvent( QWheelEvent *e ); //systray requires access

    public slots:
        void slotConfigShortcuts();
        void slotConfigGlobalShortcuts();
        void slotCopyClipboard();
        void slotConfigPlayObject();
        void slotUpdateTrayIcon( bool visible );
        void createVis();
        void slotReportBug();

    signals:
        void sigAboutToHide();
        void sigAboutToShow();

    private:
        void initScroll();
        void initTimeDisplay();
        void polish();

        void paintEvent( QPaintEvent *e );
        void mouseReleaseEvent( QMouseEvent *e );
        void mousePressEvent( QMouseEvent *e );
        void queryClose();
        void closeEvent( QCloseEvent *e );
        void moveEvent( QMoveEvent *e );

        // ATTRIBUTES ------
        QString m_bitrate, m_samplerate;
        QTimer *scrollTimer;
        QTimer *m_visTimer;
        QBoxLayout *m_pLay6;
        
        QPixmap m_oldBgPixmap;
        QPixmap *m_pScrollPixmap;
        QPixmap *m_pBgPixmap;
        QPixmap *m_pComposePixmap;
        QBitmap *m_pScrollMask;
        QPixmap *m_pTimePixmap;
        QPixmap *m_pTimeBgPixmap;
        QPixmap *m_pTimeComposePixmap;

        KHelpMenu *m_helpMenu;

        int m_timeDisplayX;
        int m_timeDisplayY;
        int m_timeDisplayW;

        int m_pixmapWidth;
        int m_pixmapHeight;
        int m_scrollWidth;
        int m_sx;
        int m_sy;
        int m_sxAdd;
        AmarokSystray *m_pTray;
        AmarokDcopHandler *m_pDcopHandler;

};

#endif
