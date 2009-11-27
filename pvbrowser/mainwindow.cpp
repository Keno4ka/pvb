/****************************************************************************
**
** Copyright (C) 2000-2006 Lehrig Software Engineering.
**
** This file is part of the pvbrowser project.
**
** This file may be used under the terms of the GNU General Public
** License version 2.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of
** this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/
#include "pvdefine.h"
#include <QtGui>
#include <stdio.h>
#include "opt.h"
#include "dlgopt.h"
#include "dlgtextbrowser.h"
#include "tcputil.h"
#include "mainwindow.h"
#include <QPrintDialog>
#include <QPrinter>

extern OPT opt;

extern QString l_file;
extern QString l_options;
extern QString l_new_window;
extern QString l_reconnect;
extern QString l_save_as_bmp;
extern QString l_log_as_bmp;
extern QString l_log_as_pvm;
extern QString l_print;
extern QString l_new_tab;
extern QString l_delete_tab;
extern QString l_exit;
extern QString l_edit;
extern QString l_copy;
extern QString l_view;
extern QString l_editmenu;
extern QString l_toolbar;
extern QString l_statusbar;
extern QString l_maximized;
extern QString l_fullscreen;
extern QString l_help;
extern QString l_manual;
extern QString l_about;

extern QString l_status_connection_lost;
extern QString l_status_connected;
extern QString l_status_could_not_connect;
extern QString l_status_reconnect;
extern QString l_status_options;
extern QString l_status_new_window;
extern QString l_status_save_as_bmp;
extern QString l_status_log_as_bmp;
extern QString l_status_log_as_pvm;
extern QString l_status_print;
extern QString l_status_new_tab;
extern QString l_status_exit;
extern QString l_status_copy;
extern QString l_status_editmenu;
extern QString l_status_toolbar;
extern QString l_status_statusbar;
extern QString l_status_toggle_maximized;
extern QString l_status_toggle_full_screen;
extern QString l_status_manual;
extern QString l_status_about;

extern QString l_print_header;

void MyThread::run()
{
  struct timeval timout;
  fd_set rset;
  int    ret,maxfd,i,s,num_tabs,emit_count;

  if(pv == NULL) exit();
  emit_count = 1; // constructor of pv has called semaphore.release() already
  connect(this, SIGNAL(dataReady(int)), pv, SLOT(dataReceived(int)));
  while(opt.closed == 0)
  {
    //printf("thread threadId = %ld\n", (long) QThread::currentThreadId());
    maxfd = pv->maxfd;
    if(maxfd < 1)
    {
      msleep(100);
    }
    else
    {
      if(emit_count == 1) pv->semaphore.acquire(); // slot dataReceived() will call semaphore.release(); before return
      FD_ZERO(&rset);
      num_tabs = pv->numTabs;
      maxfd = -1;
      for(i=0; i<num_tabs; i++)
      {
        s = pv->pvbtab[i].s;
        if(s != -1) FD_SET (s,&rset);
        if(s > maxfd) maxfd = s;
      }
      timout.tv_sec  = 0;
      timout.tv_usec = 500;
      //ret = ::select(pv->maxfd+1,&rset,NULL,NULL,&timout);
      if(maxfd == -1)
      {
        ret = ::select(1,NULL,NULL,NULL,&timout);
      }
      else
      {
        ret = ::select(maxfd+1,&rset,NULL,NULL,&timout);
      }
      //num_tabs = pv->numTabs;
      emit_count = 0;
      if(ret > 0)
      { // data available
        for(i=0; i<num_tabs; i++)
        {
          s = pv->pvbtab[i].s;
          if(s != -1)
          {
            if(FD_ISSET(s,&rset))
            {
              emit dataReady(i);
              if(emit_count > 0) pv->semaphore.acquire(); // slot dataReceived() will call semaphore.release(); before return
              emit_count++;
            }
          }
        }
      }
      else if(ret < 0)
      { // error
        printf("Thread select ERROR ret=%d\n", ret);
      }
      else
      { // timeout
        emit_count = 0;
      }
    }
  }
}

#ifdef BROWSERPLUGIN
  int pvbinit();
#endif

MainWindow::MainWindow()
{
  const char *cptr;
  int i;

#ifdef BROWSERPLUGIN
  pvbinit();
#endif

  isReconnect = 0;
  textbrowser = NULL;
  tabToolBar  = NULL;
  maxfd = currentTab = numTabs = 0;
  for(i=0; i<MAX_TABS; i++)
  {
    pvbtab[i].s         = -1;      // socket
    pvbtab[i].in_use    = 0;       // tab is currently not used
    pvbtab[i].w         = 1280;    // default width
    pvbtab[i].h         = 1024;    // default height
    pvbtab[i].view      = NULL;
    pvbtab[i].hasLayout = 0;
    for(int ii=0; ii<MAX_DOCK_WIDGETS; ii++) 
    {
      pvbtab[i].dock[ii] = NULL;
    }  
  }

  cptr = readIniFile();
  if(cptr != NULL)
  {
    QMessageBox::warning(NULL,"MainWindow","readIniFile ERROR: terminating ...");
    printf("readIniFile ERROR=%s\n",cptr);
    exit(-1);
  }

  createActions();
  createToolBars();
  createMenus();
  createStatusBar();
  if(opt.menubar   == 0) menuBar()->hide();
  if(opt.toolbar   == 0) fileToolBar->hide();
  if(opt.statusbar == 0) statusBar()->hide();

  setCurrentFile("");
  readHosts();

  semaphore.release();
  mythread.pv = this;
  mythread.start();
  // there is a problem within the firefox plugin on windows
  // we do not get enough CPU time there
  // no problem on linux
  // we get more CPU time if we continiously move the mouse over the widget
  // problem not solved jet
  //mythread.start(QThread::HighestPriority); // this does not help also

  // setup watchdog
  timer = new QTimer();
  QObject::connect(timer, SIGNAL(timeout()), this, SLOT(slotTimeOut()));
  timer->start(1000*10);

  if(opt.arg_host[0] != '\0') url = opt.arg_host;
  else                        url = opt.initialhost;
  add_host(url.toAscii());
  //winmurx moved to main() slotReconnect();
  //winmurx moved to main() slotTimeOut(); // send @hello VERSION

  if(opt.arg_x != -1 && opt.arg_y != -1 && opt.arg_w != -1 && opt.arg_h != -1)
  {
    setGeometry(opt.arg_x,opt.arg_y, opt.arg_w,opt.arg_h);
  }
  else if(opt.fullscreen)
  {
    showFullScreen();
  }
  else if(opt.maximized)
  {
    showMaximized();
  }
  else
  {
    resize(800,600);
  }
  textbrowser = new dlgTextBrowser;
}

MainWindow::~MainWindow()
{
  if(opt.arg_debug) printf("MainWindow::~MainWindow()\n");
  if(textbrowser != NULL) delete textbrowser;
  for(int i=0; i<MAX_TABS; i++)
  {
    if(pvbtab[i].s != -1) 
    {
      tcp_close(&pvbtab[i].s);
      pvbtab[i].s = -1;
      for(int ii=0; ii<MAX_DOCK_WIDGETS; ii++) 
      {
        if(pvbtab[i].dock[ii] != NULL) 
        {
          delete pvbtab[i].dock[ii];
        }
      }  
    }
  }
  if(mythread.isRunning())
  {
    mythread.terminate();
    mythread.wait();
  }
}

void MainWindow::slotExit()
{
  int i;
  if(opt.arg_debug) printf("MainWindow::slotExit()\n");
  for(i=0; i<MAX_TABS; i++)
  {
    if(pvbtab[i].s != -1) 
    {
      tcp_close(&pvbtab[i].s);
      pvbtab[i].s = -1;
    }  
  }
  close();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  int i;
  if(opt.arg_debug) printf("MainWindow::closeEvent()\n");
  if(opt.exitpassword == 1)
  {
    bool ok;
    QString pass = QInputDialog::getText(this,tr("pvbrowser"),tr("Exit Password ?"),QLineEdit::Password,QString::null,&ok);
    if( ok && !pass.isEmpty() ) 
    {
      // user entered something and pressed OK
      FILE *fp;
      fp = fopen(passfile(),"r");
      if(fp == NULL)
      {
        fp = fopen(passfile(),"w");
        if(fp != NULL)
        {
          fprintf(fp,"%s\n",pvpass(pass.toAscii()));
          fclose(fp);
        }
        opt.closed = 1;
        for(i=0; i<MAX_TABS; i++)
        {
          if(pvbtab[i].s != -1) 
          {
            tcp_close(&pvbtab[i].s);
            pvbtab[i].s = -1;
          }  
        }
        semaphore.release();
        mythread.wait();
        event->accept();
      }
      else
      {
        const char *cptr;
        char buf[1024];
        cptr = fgets(buf,sizeof(buf)-1,fp);
        fclose(fp);
        if(cptr != NULL && strncmp(buf,pvpass(pass.toAscii()),strlen(buf)-1) != 0)
        {
          QMessageBox::information(this,"pvbrowser","Wrong Password",1);
          event->ignore();
        }
      }
    }
    else
    {
      event->ignore();
    }
  }
  else
  {
    opt.closed = 1;
    for(i=0; i<MAX_TABS; i++)
    {
      if(pvbtab[i].s != -1) 
      {
        tcp_close(&pvbtab[i].s);
        pvbtab[i].s = -1;
      }  
    }
    if(opt.arg_debug) printf("MainWindow::closeEvent(): thread.terminate\n");
    if(opt.arg_debug) printf("MainWindow::closeEvent(): thread.release\n");
    semaphore.release();
    if(opt.arg_debug) printf("MainWindow::closeEvent(): thread.wait\n");
    mythread.wait();
    if(opt.arg_debug) printf("MainWindow::closeEvent(): event->accept\n");
    event->accept();
  }
}

void MainWindow::slotManual()
{
  //if(textbrowser != NULL) delete textbrowser;
  //dlgTextBrowser *dlg = textbrowser;
  //dlg = new dlgTextBrowser;
  //dlg->show();
  //tb textbrowser->form->textBrowser->setSource(QUrl::fromLocalFile("index.html"));
  //tb textbrowser->form->textBrowser->reload();
  textbrowser->form->textBrowser->load(QUrl("index.html"));
  textbrowser->show();
}

void MainWindow::about()
{
 QMessageBox::about(this, tr("About pvbrowser"),
            tr(
               "pvbrowser (R) \nVersion " VERSION
               "\n(C) 2000-2009 Lehrig Software Engineering"
               "\nlehrig@t-online.de"
               "\nhttp://pvbrowser.org"
               "\nhttp://www.lehrig.de"
               "\n"
               "\ndeveloped under Linux for:"
               "\nLinux/Unix/Windows/OS-X"
               "\n+pvserver under OpenVMS"
             ));
}

void MainWindow::createActions()
{
  if(opt.arg_debug) printf("createActions\n");

  if(opt.arg_disable == 0)
  {
    optionAct = new QAction(QIcon(":/images/option.png"), l_options, this);
    optionAct->setShortcut(tr("Ctrl+O"));
    optionAct->setStatusTip(l_status_options);
    connect(optionAct, SIGNAL(triggered()), this, SLOT(slotFileOpt()));
  }

  if(opt.arg_disable == 0)
  {
    windowAct = new QAction(QIcon(":/images/window.png"), l_new_window, this);
    windowAct->setShortcut(tr("Ctrl+N"));
    windowAct->setStatusTip(l_status_new_window);
    connect(windowAct, SIGNAL(triggered()), this, SLOT(slotWindow()));

    newtabAct = new QAction(QIcon(":/images/newtab.png"), l_new_tab, this);
    //newtabAct->setShortcut(tr("Ctrl+N"));
    newtabAct->setStatusTip(l_status_new_tab);
    connect(newtabAct, SIGNAL(triggered()), this, SLOT(slotNewTab()));
  }

  reconnectAct = new QAction(QIcon(":/images/reconnect.png"), l_reconnect, this);
  reconnectAct->setShortcut(tr("Ctrl+R"));
  reconnectAct->setStatusTip(l_status_reconnect);
  connect(reconnectAct, SIGNAL(triggered()), this, SLOT(slotReconnect()));

  storebmpAct = new QAction(QIcon(":/images/storebmp.png"), l_save_as_bmp, this);
  storebmpAct->setShortcut(tr("Ctrl+B"));
  storebmpAct->setStatusTip(l_status_save_as_bmp);
  connect(storebmpAct, SIGNAL(triggered()), this, SLOT(slotStorebmp()));

  logbmpAct = new QAction(QIcon(":/images/logbmp.png"), l_log_as_bmp, this);
  logbmpAct->setStatusTip(l_status_log_as_bmp);
  connect(logbmpAct, SIGNAL(triggered()), this, SLOT(slotLogbmp()));

  logpvmAct = new QAction(QIcon(":/images/logpvm.png"), l_log_as_pvm, this);
  logpvmAct->setStatusTip(l_status_log_as_pvm);
  connect(logpvmAct, SIGNAL(triggered()), this, SLOT(slotLogpvm()));

  printAct = new QAction(QIcon(":/images/print.png"), l_print, this);
  printAct->setShortcut(tr("Ctrl+P"));
  printAct->setStatusTip(l_status_print);
  connect(printAct, SIGNAL(triggered()), this, SLOT(slotPrint()));

  newtabActToolBar = new QAction(QIcon(":/images/newtab.png"), l_new_tab, this);
  newtabActToolBar->setStatusTip(l_status_new_tab);
  connect(newtabActToolBar, SIGNAL(triggered()), this, SLOT(slotNewTab()));

  exitAct = new QAction(QIcon(":/images/exit.png"), l_exit, this);
  exitAct->setShortcut(tr("Ctrl+Q"));
  exitAct->setStatusTip(l_status_exit);
  connect(exitAct, SIGNAL(triggered()), this, SLOT(slotExit()));

  copyAct = new QAction(QIcon(":/images/copy.png"), l_copy, this);
  copyAct->setShortcut(tr("Ctrl+C"));
  copyAct->setStatusTip(l_status_copy);
  connect(copyAct, SIGNAL(triggered()), this, SLOT(slotCopy()));

  if(opt.arg_disable == 0)
  {
    editmenuAct = new QAction(l_editmenu, this);
    editmenuAct->setShortcut(tr("Ctrl+E"));
    editmenuAct->setStatusTip(l_status_editmenu);
    connect(editmenuAct, SIGNAL(triggered()), this, SLOT(slotEditmenu()));
    addAction(editmenuAct);

    toolbarAct = new QAction(QIcon(":/images/toolbar.png"), l_toolbar, this);
    toolbarAct->setShortcut(tr("Ctrl+T"));
    toolbarAct->setStatusTip(l_status_toolbar);
    connect(toolbarAct, SIGNAL(triggered()), this, SLOT(slotToolbar()));
    addAction(toolbarAct);

    statusbarAct = new QAction(QIcon(":/images/statusbar.png"), l_statusbar, this);
    statusbarAct->setShortcut(tr("Ctrl+S"));
    statusbarAct->setStatusTip(l_status_statusbar);
    connect(statusbarAct, SIGNAL(triggered()), this, SLOT(slotStatusbar()));
    addAction(statusbarAct);

    maximizedAct = new QAction(l_maximized, this);
    maximizedAct->setShortcut(tr("Ctrl+M"));
    maximizedAct->setStatusTip(l_status_toggle_maximized);
    connect(maximizedAct, SIGNAL(triggered()), this, SLOT(slotMaximized()));
    addAction(maximizedAct);

    fullscreenAct = new QAction(QIcon(":/images/fullscreen.png"), l_fullscreen, this);
    fullscreenAct->setShortcut(tr("Ctrl+F"));
    fullscreenAct->setStatusTip(l_status_toggle_full_screen);
    connect(fullscreenAct, SIGNAL(triggered()), this, SLOT(slotFullscreen()));
    addAction(fullscreenAct);
  }

  manualAct = new QAction(l_manual, this);
  manualAct->setShortcut(tr("Ctrl+H"));
  manualAct->setStatusTip(l_status_manual);
  connect(manualAct, SIGNAL(triggered()), this, SLOT(slotManual()));

  aboutAct = new QAction(l_about, this);
  aboutAct->setStatusTip(l_status_about);
  connect(aboutAct, SIGNAL(triggered()), this, SLOT(about()));

  aboutQtAct = new QAction("About &Qt", this);
  aboutQtAct->setStatusTip("About Qt library");
  connect(aboutQtAct, SIGNAL(triggered()), qApp, SLOT(aboutQt()));

  logoAct = new QAction(QIcon(":/images/app.png"),"pvbrowser", this);
  logoAct->setStatusTip(tr("About pvbrowser"));
  connect(logoAct, SIGNAL(triggered()), this, SLOT(about()));
}

void MainWindow::createMenus()
{
  if(opt.arg_debug) printf("createMenus\n");

  fileMenu = menuBar()->addMenu(l_file);
  if(opt.arg_disable == 0)
  {
    fileMenu->addAction(optionAct);
    fileMenu->addSeparator();
    fileMenu->addAction(windowAct);
    fileMenu->addAction(newtabAct);
  }
  fileMenu->addAction(reconnectAct);
  fileMenu->addSeparator();
  fileMenu->addAction(storebmpAct);
  fileMenu->addAction(logbmpAct);
  fileMenu->addAction(logpvmAct);
  fileMenu->addAction(printAct);
  fileMenu->addSeparator();
  fileMenu->addAction(exitAct);

  editMenu = menuBar()->addMenu(l_edit);
  editMenu->addAction(copyAct);

  if(opt.arg_disable == 0)
  {
    viewMenu = menuBar()->addMenu(l_view);
    viewMenu->addAction(maximizedAct);
    viewMenu->addAction(editmenuAct);
    viewMenu->addAction(toolbarAct);
    viewMenu->addAction(statusbarAct);
    viewMenu->addAction(fullscreenAct);
  }

  menuBar()->addSeparator();

  helpMenu = menuBar()->addMenu(l_help);
  helpMenu->addAction(manualAct);
  helpMenu->addAction(aboutAct);
  helpMenu->addAction(aboutQtAct);
}

void MainWindow::createToolBars()
{
  int i;
  if(opt.arg_debug) printf("createToolbar\n");

  // begin tabbed version
  currentTab = 0;
  numTabs = 1; //MAX_TABS; //1;
  for(i=0; i<MAX_TABS; i++)
  {
    pvbtab[i].interpreter.temp = opt.temp;
    pvbtab[i].interpreter.registerMainWindow(this,&pvbtab[i].s);
    pvbtab[i].view   = new MyQWidget(&pvbtab[i].s,0,this);
  }
  pvbtab[0].in_use = 1;
  view   = pvbtab[0].view;
  scroll = new QScrollArea();
  setCentralWidget(scroll);
  fileToolBar = addToolBar(tr("File"));
  addToolBarBreak(); 
  tabToolBar  = addToolBar(tr("Tabs"));
  tabBar      = new QTabBar();
  deleteTab   = new QPushButton();
  deleteTab->setToolTip(l_delete_tab);
  deleteTab->setIcon(QIcon(":/images/deletetab.png"));
  deleteTab->setFixedSize(18,18);
  tabBar->addTab(tr("Tab0"));
  tabBar->setTabWhatsThis(0, "0");
  tabToolBar->addWidget(tabBar);
  tabToolBar->addWidget(deleteTab);
  connect(tabBar,    SIGNAL(currentChanged(int)), this, SLOT(slotTabChanged(int)));
  connect(deleteTab, SIGNAL(clicked()),           this, SLOT(slotDeleteTab()));
  tabToolBar->hide();
  // end tabbed version

  fileToolBar->addAction(storebmpAct);
  fileToolBar->addAction(logbmpAct);
  fileToolBar->addAction(logpvmAct);
  fileToolBar->addAction(printAct);
  fileToolBar->addAction(newtabActToolBar);
  fileToolBar->addSeparator();

  fileToolBar->addAction(copyAct);
  fileToolBar->addSeparator();

  urlComboBox = new QComboBox(NULL);
  urlComboBox->setMinimumWidth(400);
  urlComboBox->setMaximumWidth(500);
  urlComboBox->setEditable(true);
  urlComboBox->setDuplicatesEnabled(false);
  //urlComboBox->setAutoCompletion(false);
  urlComboBox->setToolTip(tr("Connect to host:\n"
                             "pv://host<:port></mask>\n"
                             "pvssh://host<:port></mask>\n"
                             "example: pv://localhost\n"
                             "example: pv://localhost:5050\n"
                             "example: pv://localhost:5050/maskname\n"
                             "http://host"
                             ));
  fileToolBar->addWidget(urlComboBox);
  connect(urlComboBox, SIGNAL(activated(const QString &)), this, SLOT(slotUrl(const QString &)));
  fileToolBar->addSeparator();

  whatsthisAct = new QAction(QIcon(":/images/whatsthis.png"), "whatsThis", this);
  fileToolBar->addAction(whatsthisAct);
  connect(whatsthisAct, SIGNAL(triggered()), this, SLOT(slotWhatsThis()));
  fileToolBar->addSeparator();

  logoLabel = new QLabel(NULL);
  QPixmap pm(opt.customlogo);
  if(pm.isNull())
  {
    printf("customlogo=%s not found\n",opt.customlogo);
    return;
  }
  logoLabel->setPixmap(pm);
  fileToolBar->addAction(logoAct);
  fileToolBar->insertWidget(logoAct,logoLabel);
}

void MainWindow::slotTabChanged(int index)
{
  QWidget *w;
  QString text;
  int  ipvbtab, ww, hh, ii;
  char buf[32];

  if(opt.arg_debug) printf("old_url=%s\n",(const char *) pvbtab[currentTab].url.toAscii());
  if(pvbtab[currentTab].s != -1) // pause old tab
  {
    sprintf(buf,"@pause(%d)\n",1);
    if(tcp_send(&pvbtab[currentTab].s,buf,strlen(buf)) == -1)
    { // connection lost
      tcp_close(&pvbtab[currentTab].s);
      pvbtab[currentTab].s = -1;
      QPalette palette(QColor(255,0,0));
      statusBar()->setPalette(palette);
      statusBar()->showMessage(l_status_connection_lost);
      if(opt.arg_debug) printf("view->setDisabled1 tcp_close(%d)\n", currentTab);
      view->setDisabled(1);
      qApp->beep();
    }
  }
  w = centralWidget();
  pvbtab[currentTab].view = (MyQWidget *) ((QScrollArea *) w)->takeWidget();
  text = tabBar->tabWhatsThis(index);
  sscanf((const char *) text.toAscii(),"%d", &ipvbtab);
  if(opt.arg_debug) printf("Tab changed to index=%d ipvbtab=%d begin\n", index, ipvbtab);
  for(ii=0; ii<MAX_DOCK_WIDGETS; ii++) // perhaps hide the docks
  {
    MyQDockWidget *dock = pvbtab[currentTab].dock[ii]; 
    if(dock != NULL)
    {
      if(opt.arg_debug) printf("removeDock %d\n", ii);
      removeDockWidget(dock);
    }  
  }  

  currentTab = ipvbtab;
  if(opt.arg_debug) printf("new_url=%s\n",(const char *) pvbtab[currentTab].url.toAscii());
  urlComboBox->setEditText(pvbtab[currentTab].url);
  ww = pvbtab[currentTab].interpreter.width();
  hh = pvbtab[currentTab].interpreter.height();
  if(pvbtab[currentTab].view == NULL)
  {
    pvbtab[currentTab].view = new MyQWidget(&pvbtab[currentTab].s,0,this);
  }  
  pvbtab[currentTab].view->resize(ww,hh);
  scroll->resize(ww,hh);
  scroll->setWidget(pvbtab[currentTab].view);
  // workaround for qt
  if(pvbtab[currentTab].interpreter.hasLayout == 1) scroll->setWidgetResizable(true);
  else                                              scroll->setWidgetResizable(false);
  QApplication::postEvent(this, new QResizeEvent(QSize(ww-1,hh-1),QSize(ww,hh))); // force qt to update slider

  if(pvbtab[currentTab].s != -1) // unpause new tab
  {
    sprintf(buf,"@pause(%d)\n",0);
    if(tcp_send(&pvbtab[currentTab].s,buf,strlen(buf)) == -1)
    { // connection lost
      tcp_close(&pvbtab[currentTab].s);
      pvbtab[currentTab].s = -1;
      QPalette palette(QColor(255,0,0));
      statusBar()->setPalette(palette);
      statusBar()->showMessage(l_status_connection_lost);
      if(opt.arg_debug) printf("view->setDisabled2 tcp_close(%d)\n", currentTab);
      view->setDisabled(1);
      qApp->beep();
    }
  }
  for(ii=0; ii<MAX_DOCK_WIDGETS; ii++) // perhaps show the docks
  {
    MyQDockWidget *dock = pvbtab[currentTab].dock[ii]; 
    if(dock != NULL) 
    {
      if(opt.arg_debug) printf("addDock %d\n", ii);
      addDockWidget(Qt::LeftDockWidgetArea,dock);
      dock->show();
    }  
  }  
  if(opt.arg_debug) printf("Tab changed to index=%d ipvbtab=%d end\n", index, ipvbtab);
}

void MainWindow::setTabText(const char *title)
{
  int i,ipvbtab;
  QString text;

  for(i=0; i<numTabs; i++)
  {
    text = tabBar->tabWhatsThis(i);
    sscanf((const char *) text.toAscii(),"%d", &ipvbtab);
    if(ipvbtab == currentTab)
    {
      tabBar->setTabText(i,title);
      break;
    }
  }
}

void MainWindow::slotNewTab()
{
  QString text;
  int i,index;

  if(tabBar->count() < MAX_TABS)
  {
    for(i=0; i<MAX_TABS; i++)
    {
      if(pvbtab[i].in_use == 0)
      {
        pvbtab[i].in_use = 1;
        pvbtab[i].url = opt.initialhost;
        pvbtab[i].hasLayout = 0;
        urlComboBox->setEditText(pvbtab[i].url);
        if(opt.arg_debug) printf("new_tab_url=%s\n",(const char *) pvbtab[i].url.toAscii());
        break;
      }  
    }
    index = tabBar->addTab("NewTab");
    text.sprintf("%d", i);
    tabBar->setTabWhatsThis(index, text);
    tabBar->setCurrentIndex(index); 
  }
  else
  {
    QMessageBox::information(this, "pvbrowser", tr("No more tabs available"));
  }
  tabToolBar->hide();
  tabToolBar->show();
}

void MainWindow::slotDeleteTab()
{
  QWidget *w;
  QString  text;
  int index, ipvbtab;

  index = tabBar->currentIndex();
  text = tabBar->tabWhatsThis(index);
  sscanf((const char *) text.toAscii(),"%d", &ipvbtab);
  if(opt.arg_debug) printf("slotDeleteTab ipvbtab=%d\n", ipvbtab);
  pvbtab[ipvbtab].in_use = 0;
  if(pvbtab[ipvbtab].s != -1)
  {
    for(int ii=0; ii<MAX_DOCK_WIDGETS; ii++) // perhaps delete docks
    {
      if(pvbtab[currentTab].dock[ii] != NULL) 
      {
        if(opt.arg_debug) printf("deleteDock %d\n", ii);
        delete pvbtab[currentTab].dock[ii];
        pvbtab[currentTab].dock[ii] = NULL;
      }
    }  
    tcp_close(&pvbtab[ipvbtab].s);
    pvbtab[ipvbtab].s = -1;
    if(ipvbtab == currentTab && view != NULL) view->setDisabled(1);
    w = centralWidget();
    pvbtab[currentTab].view = (MyQWidget *) ((QScrollArea *) w)->takeWidget();
    delete pvbtab[currentTab].view;
    pvbtab[currentTab].view = new MyQWidget(&pvbtab[currentTab].s,0,this);
    scroll->setWidget(pvbtab[currentTab].view);
  }
  tabBar->removeTab(index);
  if(tabBar->count() == 1) tabToolBar->hide();
  else                     tabToolBar->show();
}

void MainWindow::createStatusBar()
{
  if(opt.arg_debug) printf("createStatusbar\n");
  statusBar()->setAutoFillBackground(true);
  statusBar()->showMessage(tr("Ready"));
}

void MainWindow::setCurrentFile(const QString &fileName)
{
  if(opt.arg_debug) printf("setCurrentFile\n");
  curFile = fileName;
  QString shownName;
  if (curFile.isEmpty()) shownName = "ProcessViewBrowser";
  else                   shownName = curFile;
  setWindowTitle(tr("%1[*] - %2").arg(shownName).arg(tr("pvbrowser " VERSION)));
  setTabText(shownName.toUtf8());
}

void MainWindow::slotEditmenu()
{
  if(opt.arg_debug) printf("slotEditmenu\n");
  if(menuBar()->isVisible()) menuBar()->hide();
  else                       menuBar()->show();
}

void MainWindow::slotToolbar()
{
  if(opt.arg_debug) printf("slotToolbar\n");
  if(fileToolBar->isVisible()) fileToolBar->hide();
  else                         fileToolBar->show();
}

void MainWindow::slotStatusbar()
{
  if(opt.arg_debug) printf("slotStatusbar\n");
  if(statusBar()->isVisible()) statusBar()->hide();
  else                         statusBar()->show();
}

void MainWindow::slotMaximized()
{
  if(opt.arg_debug) printf("slotMaximized\n");
  if(isMaximized()) showNormal();
  else              showMaximized();
}

void MainWindow::slotFullscreen()
{
  if(opt.arg_debug) printf("slotFullscreen\n");
  if(isFullScreen()) showNormal();
  else               showFullScreen();
}

void MainWindow::slotFileOpt()
{
int ret,cnt,i;

  if(opt.arg_debug) printf("slotFileOpt\n");
  DlgOpt *dlg = new DlgOpt(this);
  dlg->setWindowTitle("pvbrowser options");
  dlg->setFilename(inifile());
  ret = dlg->exec();
  if(ret == QDialog::Accepted)
  {
    cnt = urlComboBox->count();
    for(i=0; i<cnt; i++)
    {
      urlComboBox->removeItem(0);
    }
    readIniFile();
    for(i=0; i<MAX_TABS; i++)
    {
      pvbtab[i].interpreter.temp = opt.temp;
    }
    readHosts();
  }
  delete dlg;
}

void MainWindow::slotWhatsThis()
{
  if(QWhatsThis::inWhatsThisMode()) QWhatsThis::leaveWhatsThisMode();
  else                              QWhatsThis::enterWhatsThisMode();
}

void MainWindow::slotWindow()
{
  int ret = 0;
#ifdef PVUNIX
  char buf[1024];
  if(opt.arg_debug) printf("slotWindow opt.newwindow=%s\n",opt.newwindow);
  strcpy(buf,opt.newwindow);
  strcat(buf," &");
  //printf("before system(%s)\n",opt.newwindow);
  if(opt.newwindow[0] == '\0') ret = system("pvbrowser &");
  else                         ret = system(buf);
  //printf("after system ret=%d\n",ret);
#endif

#ifdef __VMS
  char buf[1024];
  strcpy(buf,"spawn/nowait ");
  strcat(buf,opt.newwindow);
  if(opt.newwindow[0] == '\0') ret = system("spawn/nowait pvbrowser");
  else                         ret = system(buf);
#endif

#ifdef PVWIN32
  if(opt.newwindow[0] == '\0') ret = mysystem("pvbrowser");
  else                         ret = mysystem(opt.newwindow);
#endif
  if(ret < 0) return;
}

void MainWindow::slotReconnect()
{
  char buf[MAX_PRINTF_LENGTH],host[MAX_PRINTF_LENGTH],sshstring[MAX_PRINTF_LENGTH],*cptr;
  QString qbuf;
  int iport,i,ssh,max,maxtab;

  if(opt.arg_debug) printf("slotReconnect url=%s\n",(const char *) url.toAscii());
  pvbtab[currentTab].url = url;
  ssh = 0;
  sshstring[0] = '\0';
  url.truncate(sizeof(buf) - 80);
  strcpy(buf,url.toAscii());

  if(strncmp(buf,"http://",7) == 0 || strncmp(buf,"https://",8) == 0) 
  {
    if(pvbtab[currentTab].s != -1) 
    {
      tcp_close(&pvbtab[currentTab].s);
      pvbtab[currentTab].s = -1;
    }  
    pvbtab[currentTab].interpreter.showMyBrowser(buf);
    add_host(buf);
    return;
  }

  do // eliminate windows slosh'es
  {
    cptr = strchr(buf,'\\');
    if(cptr != NULL) *cptr = '/';
  }
  while(cptr != NULL);
  if(buf[strlen(buf)-1] == '/') buf[strlen(buf)-1] = '\0';

  if     (strncmp(buf,"ssh://",6) == 0 || strncmp(buf,"pvssh://",8) == 0)
  {
    char ssh_user_host[MAX_PRINTF_LENGTH];
    char ssh_host[MAX_PRINTF_LENGTH];
    int  ssh_port;
    ssh = 1;
    strcpy(sshstring,buf);
    sprintf(host,"localhost:%d",opt.sshport); // we connect locally
    strcpy(ssh_host,"localhost");
    ssh_port = opt.port;
    cptr = &buf[6]; 
    if(buf[0] == 'p') cptr = &buf[8];
    i = 0;
    while(*cptr != ':' && *cptr != '\0') ssh_user_host[i++] = *cptr++;
    ssh_user_host[i] = '\0';
    if(*cptr == ':')
    {
      cptr++;
      if(isalpha(*cptr) || strchr(cptr,'.') != NULL)
      {
        i = 0;
        while(*cptr != ':' && *cptr != '\0') ssh_host[i++] = *cptr++;
        ssh_host[i] = '\0';
        cptr++;
      }
      if(*cptr != '\0') sscanf(cptr,"%d",&ssh_port);  
    }
    if(isReconnect == 0)
    {
      QString qbuf;
#ifdef PVUNIX    
      qbuf.sprintf("xterm -e %s -L %d:%s:%d %s &",opt.ssh,opt.sshport,ssh_host,ssh_port,ssh_user_host);
#endif
#ifdef PVWIN32
      qbuf.sprintf("%s -ssh -L %d:%s:%d %s",opt.ssh,opt.sshport,ssh_host,ssh_port,ssh_user_host);
#endif
      if(opt.arg_debug) printf("mysystem(%s)\n",(const char *) qbuf.toAscii());
      mysystem(qbuf.toAscii());
    }
  }
  else if(strncmp(buf,"pv://"  ,5) == 0) 
  {
    strcpy(host,&buf[5]);
  }  
  else
  {
    strcpy(host,buf);
  }  

  if(host[0] == '\0') return;

  if(pvbtab[currentTab].s != -1) 
  {
    tcp_close(&pvbtab[currentTab].s);
    pvbtab[currentTab].s = -1;
  }  
  strcpy(buf,host);
  iport = opt.port;
  if(buf[0] == '[')
  { // plain IPV6 address as number
    strcpy(buf,&host[1]);
    cptr = strchr(buf,']');
    if(cptr != NULL)
    {
      *cptr = '\0';
      cptr++;
      if(*cptr == ':') // port is given
      {
        cptr++;
        sscanf(cptr,"%d",&iport);
      }
    }
  }
  else
  {
    cptr = strchr(buf,':'); // if special port is given
    if(cptr != NULL)
    {
      *cptr++ = '\0';
      sscanf(cptr,"%d",&iport);
    }
  }
  cptr = strstr(buf,"/");
  if(cptr != NULL) *cptr = '\0';
  QApplication::setOverrideCursor( Qt::WaitCursor );
  pvbtab[currentTab].s = tcp_con(buf,iport);
  max = maxtab= 0;
  for(i=0; i<MAX_TABS; i++)
  {
    if(pvbtab[i].s > max) max = pvbtab[i].s;
    if(pvbtab[i].s != -1) maxtab = i;
  }
  maxfd = max;
  numTabs = maxtab+1;
  if(opt.arg_debug) printf("slotReconnect tcp_con s=%d host=%s:%d maxfd=%d\n",pvbtab[currentTab].s,buf,iport,maxfd);
  QApplication::restoreOverrideCursor();

  cptr = strstr(host,"/"); // send initial mask
  if(cptr == NULL)
  {
    sprintf(buf,"initial(0,\"\") version=%s\n",VERSION);
    tcp_send(&pvbtab[currentTab].s,buf,strlen(buf));
  }
  else
  {
    cptr++;
    sprintf(buf,"initial(0,\"%s\") version=%s\n",cptr,VERSION);
    tcp_send(&pvbtab[currentTab].s,buf,strlen(buf));
  }

  // send initial url
  sprintf(buf,"@url=%s\n", (const char *) url.toAscii());
  tcp_send(&pvbtab[currentTab].s,buf,strlen(buf));

  if(pvbtab[currentTab].s != -1)
  {
    if(ssh == 0)
    {
      qbuf  = l_status_connected;
      qbuf += " ";
      qbuf += host;
    }
    else if(ssh == 1)
    {
      qbuf  = l_status_connected;
      qbuf += " ";
      qbuf += sshstring;
    }
    QPalette palette;
    statusBar()->setPalette(palette);
    statusBar()->showMessage(qbuf);
    if     (ssh == 0) add_host(host);
    else if(ssh == 1) add_host(sshstring);
  }
  else
  {
    if(ssh == 0)
    {
      qbuf  = l_status_could_not_connect;
      qbuf += " ";
      qbuf += host;
    }
    else if(ssh == 1)
    {
      qbuf  = l_status_could_not_connect;
      qbuf += " ";
      qbuf += sshstring;
    }
    QPalette palette(QColor(255,0,0));
    statusBar()->setPalette(palette);
    //rlehrig statusBar()->setPaletteBackgroundColor(QColor(255,0,0));
    statusBar()->showMessage(qbuf);
    qApp->beep();
  }
  isReconnect = 1;
}

void MainWindow::add_host(const char *host)
{
  char buf[MAX_PRINTF_LENGTH];

  buf[0] = '\0';
  if     (strncmp(host,"pv://",5)    == 0) ;
  else if(strncmp(host,"pvssh://",8) == 0) ;
  else if(strncmp(host,"http://",7)  == 0) ;
  else if(strncmp(host,"https://",8) == 0) ;
  else if(strncmp(host,"ssh://"  ,6) == 0) strcpy(buf,"pvssh://");
  else                                     strcpy(buf,"pv://");
  strcat(buf,host);
  appendIniFile(buf);
}

void MainWindow::readHosts()
{
  FILE *fp;
  char buf[MAX_PRINTF_LENGTH],host[MAX_PRINTF_LENGTH];

  fp = fopen(inifile(),"r");
  if(fp == NULL) return;
  while( fgets(buf,sizeof(buf)-1,fp) != NULL )
  {
    if(strncmp(buf,"host=",5) == 0)
    {
      sscanf(buf,"host=%s",host);
      if(opt.arg_debug > 0) printf("readHosts host=%s\n",host);
      urlComboBox->insertItem(0,host);
    }
  }
  fclose(fp);
}

void MainWindow::appendIniFile(const char *host)
{
  FILE *fp;
  char buf[MAX_PRINTF_LENGTH];
  int found = 0;

  if(opt.arg_debug) printf("appendIniFile(%s)\n",host);
  if(strchr(host,'?') != NULL) return; // don't remember parameters
  fp = fopen(inifile(),"r");
  if(fp == NULL) return;
  while( fgets(buf,sizeof(buf)-1,fp) != NULL )
  {
    if(strncmp(buf,"host=",5) == 0)
    {
      if(strncmp(&buf[5],host,strlen(host)) == 0)
      {
        if(opt.arg_debug) printf("appendIniFile(%s) already in list\n",host);
        found = 1;
        break; // host already in list
      }
    }
  }
  fclose(fp);

  if(found == 0)
  {
    fp = fopen(inifile(),"a");
    if(fp != NULL)
    {
      fprintf(fp,"host=%s\n",host);
      urlComboBox->insertItem(0,host);
    }
    fclose(fp);
  }

  for(int i=0; i<urlComboBox->count(); i++)
  {
    if(urlComboBox->itemText(i) == host)
    {
      urlComboBox->setCurrentIndex(i);
    }
  }
}

void MainWindow::slotTimeOut()
{
  int i;
  char buf[20];

  for(i=0; i<MAX_TABS; i++)
  {
    if(pvbtab[i].s != -1 && opt.closed == 0)
    {
      sprintf(buf,"@hello=%s\n",VERSION);
      //printf("tab=%d buf=%s",i,buf);
      if(tcp_send(&pvbtab[i].s,buf,strlen(buf)) == -1)
      { // connection lost
        tcp_close(&pvbtab[i].s);
        pvbtab[i].s = -1;
        QPalette palette(QColor(255,0,0));
        statusBar()->setPalette(palette);
        //statusBar()->setPaletteBackgroundColor(QColor(255,0,0));
        statusBar()->showMessage(l_status_connection_lost);
        if(opt.arg_debug) printf("view->setDisabled\n");
        view->setDisabled(1);
        qApp->beep();
      }
    }
  }

  if(pvbtab[currentTab].s == -1)
  {
    if(opt.autoreconnect == 1) 
    {
     isReconnect = 1;
     slotReconnect();
    }
  }
}

void MainWindow::slotStorebmp()
{
  if(opt.arg_debug) printf("slotStorebmp\n");
  QPixmap pm;
  snapshot(pm);
  QString fn = QFileDialog::getSaveFileName(0,tr("Save as BMP"), ".", "*.bmp");
  if(!fn.isEmpty())
  {
    pm.save(fn,"BMP");
  }
}

void MainWindow::dataReceived(int ind)
{
int  ret;
char buf[MAX_PRINTF_LENGTH] = "";

  //printf("data threadId = %ld\n", (long) QThread::currentThreadId());
  if(opt.arg_debug > 1) printf("rec1 ind=%d s=%d\n",ind,pvbtab[ind].s);
  if(pvbtab[ind].s == -1)
  {
    semaphore.release();
    return;
  }

  if(opt.arg_debug > 1) printf("dataReceived:");
  ret = tcp_rec(&pvbtab[ind].s, buf, sizeof(buf)-1);
  if(opt.arg_debug > 1) printf("%s",buf);

  if(ret == -1)
  {
    if(numTabs > 1)
    {
      // crash when pvservers are forced to abort -> test if own|qt|os problem
      opt.closed = 1; // terminate tcp watch/select thread
      qApp->beep();
      printf("pvserver(s) aborted abnormally: terminating ...\n");
      qApp->beep();
      QMessageBox::critical(this,"MainWindow","pvserver(s) aborted abnormally: terminating ...\nProbably the developer(s) of the corresponding pvserver(s) have restarted a modified visualization.");
      exit(-1);
    }
    tcp_close(&pvbtab[ind].s);
    pvbtab[ind].s = -1;
    QPalette palette(QColor(255,0,0));
    statusBar()->setPalette(palette);
    //statusBar()->setPaletteBackgroundColor(QColor(255,0,0));
    statusBar()->showMessage(l_status_connection_lost);
    if(opt.arg_debug) printf("view->setDisabled4\n");
    if(pvbtab[ind].view != NULL) pvbtab[ind].view->setDisabled(1); // crash when pvservers are forced to abort -> test if own|qt|os problem
    if(opt.arg_debug) printf("view->setDisabled4 end\n");
    qApp->beep();
#ifdef PVWIN32
    isReconnect = 1;
    slotReconnect();
#endif        
  }
  else
  {
    pvbtab[ind].interpreter.interpret(buf);
  }
#ifdef PVUNIX
  if(opt.arg_debug) fflush(stdout);
#endif
  semaphore.release();
  if(opt.arg_debug > 1) printf("rec2\n");
}

void MainWindow::slotLogbmp()
{
  if(opt.arg_debug) printf("slotLogbmp\n");
  pvbtab[currentTab].interpreter.metaFileBmp();
}

void MainWindow::slotLogpvm()
{
  if(opt.arg_debug) printf("slotLogpvm\n");
  pvbtab[currentTab].interpreter.metaFileLog();
}

#define LEFT_PRINT_MARGIN  10
#define UPPER_PRINT_MARGIN 70

void MainWindow::slotPrint()
{
  if(opt.arg_debug) printf("slotPrint\n");
  QWidget *w = centralWidget();
  if(w == NULL)
  {
    QMessageBox::information (this, "pvbrowser", tr("no printable contents"));
    return;
  }
  QPixmap pm;
  QPrinter printer;
  snapshot(pm);
  QPrintDialog printDialog(&printer, this);
  if(printDialog.exec() == QDialog::Accepted)
  {
    // print ...
    QPainter painter;
    painter.begin(&printer);
    if(strlen(l_print_header.toAscii()) > 0)
    {
      QString date_time;
      date_time  = l_print_header;
      date_time += QDateTime::currentDateTime().toString(Qt::TextDate);
      painter.drawText(LEFT_PRINT_MARGIN,UPPER_PRINT_MARGIN-30,1280,100,Qt::TextExpandTabs, date_time);
    }
    QPixmap pmprint = pm.scaledToWidth(painter.viewport().width()-LEFT_PRINT_MARGIN*2);
    painter.drawPixmap(LEFT_PRINT_MARGIN,UPPER_PRINT_MARGIN,pmprint);
    painter.end();
  }
}

void MainWindow::slotCopy()
{
  if(opt.arg_debug) printf("slotCopy\n");
  QPixmap pm;
  snapshot(pm);
  QApplication::clipboard()->setPixmap(pm);
}

void MainWindow::snapshot(QPixmap &pm)
{
  if(opt.arg_debug) printf("snapshot\n");
  QWidget *w = centralWidget();
  if(w != NULL)
  {
    w->raise();
    pm = QPixmap::grabWindow(w->winId(),0,0,w->width(),w->height());
  }  
  if(opt.arg_debug) printf("snapshot done\n");
}

void MainWindow::slotUrl(const QString &urlin)
{
  url = urlin;
  if(opt.arg_debug) printf("slotUrl url=%s\n",(const char*) url.toAscii());
  isReconnect = 0;
  slotReconnect();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
  int modifier,key;

  key = e->key();
  modifier = 0;
  if     (e->modifiers() == Qt::ShiftModifier)
  {
    modifier = 4;
  }
  else if(e->modifiers() == Qt::ControlModifier)
  {
    modifier = 3;
  }
  else if(e->modifiers() == Qt::AltModifier)
  {
    modifier = 2;
  }
  else if(key >= Qt::Key_Escape && key <= Qt::Key_Direction_R)
  {
    if(key != Qt::Key_Left && key != Qt::Key_Up && key != Qt::Key_Right && key != Qt::Key_Down)
    {
      modifier = 1;
    }
  }
  if(modifier != 0)
  {
    char buf[80];
    if(opt.arg_debug) printf("key modifier=%d key=%d ascii=%s\n",modifier,e->key(),(const char *) e->text().toAscii());
    sprintf(buf,"key(%d,%d,\"%s\")\n",modifier,key,(const char *) e->text().toAscii());
    tcp_send(&pvbtab[currentTab].s,buf,strlen(buf));
  }
  QMainWindow::keyPressEvent(e);
}

void MainWindow::keyReleaseEvent(QKeyEvent *e)
{
  int modifier,key;

  key = e->key();
  modifier = 0;
  if     (e->modifiers() == Qt::ShiftModifier)
  {
    modifier = -4;
  }
  else if(e->modifiers() == Qt::ControlModifier)
  {
    modifier = -3;
  }
  else if(e->modifiers() == Qt::AltModifier)
  {
    modifier = -2;
  }
  else if(key >= Qt::Key_Escape && key <= Qt::Key_Direction_R)
  {
    if(key != Qt::Key_Left && key != Qt::Key_Up && key != Qt::Key_Right && key != Qt::Key_Down)
    {
      modifier = -1;
    }
  }
  if(modifier != 0)
  {
    char buf[80];
    if(opt.arg_debug) printf("key modifier=%d key=%d ascii=%s\n",modifier,e->key(),(const char *) e->text().toAscii());
    sprintf(buf,"key(%d,%d,\"%s\")\n",modifier,key,(const char *) e->text().toAscii());
    tcp_send(&pvbtab[currentTab].s,buf,strlen(buf));
  }
  QMainWindow::keyReleaseEvent(e);
}
