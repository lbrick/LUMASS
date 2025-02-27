 /******************************************************************************
 * Created by Alexander Herzig
 * Copyright 2010,2011,2012 Landcare Research New Zealand Ltd
 *
 * This file is part of 'LUMASS', which is free software: you can redistribute
 * it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/
/*
 * NMModelScene.cpp
 *
 *  Created on: 20/06/2012
 *      Author: alex
 */

#include <QMimeData>
#include <QString>
#include <QObject>
#include <QStatusBar>
#include <QGraphicsTextItem>
#include <QGraphicsLineItem>
#include <QMessageBox>
#include <QDebug>
#include <QInputDialog>
#include <QFileInfo>
#include <QGraphicsProxyWidget>

#include "NMModelComponent.h"
#include "NMGlobalHelper.h"
#include "NMModelScene.h"
#include "NMModelViewWidget.h"
#include "NMProcessComponentItem.h"
#include "NMAggregateComponentItem.h"
#include "NMParameterTable.h"
#include "NMSqlTableModel.h"
#include "lumassmainwin.h"
//#include "nmlog.h"

#ifndef NM_ENABLE_LOGGER
#   define NM_ENABLE_LOGGER
#   include "nmlog.h"
#   undef NM_ENABLE_LOGGER
#else
#   include "nmlog.h"
#endif

class NMModelViewWidget;

const std::string NMModelScene::ctx = "NMModelScene";


NMModelScene::NMModelScene(QObject* parent)
    : QGraphicsScene(parent), mLogger(0)
{
    //ctx = "NMModelScene";
    mMode = NMS_UNDEFINED;
    mLinkHitTolerance = 15;
    mLinkZLevel = 10000;
    mLinkLine = 0;
    mHiddenModelItems.clear();
    mRubberBand = 0;
    mbIdleMove = false;
    mbIdleOpenHand = false;
}

NMModelScene::~NMModelScene()
{
}

void
NMModelScene::dragLeaveEvent(QGraphicsSceneDragDropEvent *event)
{
    NMDebugCtx(ctx, << "...");
    mMousePos = event->scenePos();

    if (event->mimeData()->hasFormat("text/plain"))
    {
        QString leaveText = event->mimeData()->text();
        if (leaveText.startsWith(QString::fromLatin1("_NMModelScene_:")))
        {
            if (mDragItemList.count() == 1)
            {
                NMProcessComponentItem* pi = qgraphicsitem_cast<NMProcessComponentItem*>(
                            mDragItemList.at(0));
                NMAggregateComponentItem* ai = qgraphicsitem_cast<NMAggregateComponentItem*>(
                            mDragItemList.at(0));
                QString compName;
                if (pi != 0)
                {
                    compName = pi->getTitle();
                }
                else if (ai != 0)
                {
                    compName = ai->getTitle();
                }

                leaveText = QString::fromLatin1("_NMModelScene_:%1").arg(compName);
                QMimeData* md = const_cast<QMimeData*>(event->mimeData());
                md->setText(leaveText);
                event->setMimeData(md);
                event->accept();
            }
        }
    }
    mDragItemList.clear();
    NMDebugCtx(ctx, << "done!");

    //QGraphicsScene::dragLeaveEvent(event);
}

void
NMModelScene::dragEnterEvent(QGraphicsSceneDragDropEvent* event)
{
    NMDebugCtx(ctx, << "...");
    mMousePos = event->scenePos();

    if (    event->mimeData()->hasFormat("text/plain")
        ||  event->mimeData()->hasUrls())
    {
        QString fileName;
        foreach(const QUrl& url, event->mimeData()->urls())
        {
            if (url.isLocalFile())
            {
                fileName = url.toLocalFile();
                NMDebugAI(<< fileName.toStdString() << std::endl);
                //break;
            }
        }

        NMDebugAI(<< event->mimeData()->text().toStdString());
        QString mimeText = event->mimeData()->text();
        if (    mimeText.startsWith(QString::fromLatin1("_NMProcCompList_:"))
            ||  mimeText.startsWith(QString::fromLatin1("_NMModelScene_:"))
            ||  mimeText.startsWith(QString::fromLatin1("_ModelComponentList_:"))
            ||  mimeText.startsWith(QString::fromLatin1("_UserModelList_:"))
            ||  !fileName.isEmpty()
           )
        {
            NMDebug(<< mimeText.toStdString() << " - supported!" << std::endl);
            event->acceptProposedAction();
        }
        else
        {
            NMDebug(<< " - unfortunately not supported!" << std::endl);
        }
    }

    NMDebugCtx(ctx, << "done!");
}

void
NMModelScene::dragMoveEvent(QGraphicsSceneDragDropEvent* event)
{
    mMousePos = event->scenePos();

    if (    event->mimeData()->hasFormat("text/plain")
        ||  event->mimeData()->hasUrls()
       )
    {
        event->acceptProposedAction();
    }
}

void
NMModelScene::toggleLinkToolButton(bool linkMode)
{
    if (linkMode)
    {
        //NMLogDebug(<< "link on" << std::endl);
        this->setProcCompMoveability(false);
        this->setProcCompSelectability(false);
        this->setLinkCompSelectability(false);
        this->mMode = NMS_LINK;
    }
//    else
//    {
//        NMLogDebug(<< "link off" << std::endl);
//    }
    updateCursor();
}

void
NMModelScene::toggleZoomInTool(bool zin)
{
    if (mRubberBand)
    {
        delete mRubberBand;
        mRubberBand = 0;
    }
    if (zin)
    {
        this->mMode = NMS_ZOOM_IN;
        this->setProcCompMoveability(false);
        this->setProcCompSelectability(false);
        this->setLinkCompSelectability(false);
    }
    updateCursor();
}

void
NMModelScene::toggleZoomOutTool(bool zout)
{
    if (mRubberBand)
    {
        delete mRubberBand;
        mRubberBand = 0;
    }
    if (zout)
    {
        this->mMode = NMS_ZOOM_OUT;
        this->setProcCompMoveability(false);
        this->setProcCompSelectability(false);
        this->setLinkCompSelectability(false);
    }
    updateCursor();
}

void NMModelScene::toggleSelToolButton(bool selMode)
{
    if (mRubberBand)
    {
        delete mRubberBand;
        mRubberBand = 0;
    }
    if (selMode)
    {
        this->mMode = NMS_SELECT;
        this->setProcCompMoveability(false);
        this->setProcCompSelectability(true);
        this->setLinkCompSelectability(false);
    }
    updateCursor();
}

void NMModelScene::toggleMoveToolButton(bool moveMode)
{
    if (moveMode)
    {
        this->mMode = NMS_MOVE;
        this->setProcCompMoveability(false);
        //this->setProcCompSelectability(false);
        this->setLinkCompSelectability(false);
    }
    updateCursor();
    this->invalidate();
}

void
NMModelScene::idleModeOn(void)
{
    this->mMode = NMS_IDLE;
    this->setProcCompMoveability(true);
    this->setProcCompSelectability(false);
    this->setLinkCompSelectability(false);
    updateCursor();
}

void
NMModelScene::updateCursor(void)
{
    QGraphicsView* v = this->views().at(0);
    switch(mMode)
    {
        case NMS_MOVE:
            QApplication::restoreOverrideCursor();
            v->setDragMode(QGraphicsView::ScrollHandDrag);
            QApplication::setOverrideCursor(Qt::OpenHandCursor);
            //v->setCursor(Qt::OpenHandCursor);
            break;

        case NMS_LINK:
            QApplication::restoreOverrideCursor();
            v->setDragMode(QGraphicsView::NoDrag);
            v->setCursor(Qt::CrossCursor);
            break;

        case NMS_SELECT:
            QApplication::restoreOverrideCursor();
            v->setDragMode(QGraphicsView::NoDrag);
            v->setCursor(Qt::ArrowCursor);
            break;
        case NMS_ZOOM_IN:
        case NMS_ZOOM_OUT:
            QApplication::restoreOverrideCursor();
            v->setDragMode(QGraphicsView::NoDrag);
            v->setCursor(Qt::CrossCursor);
            break;

        case NMS_IDLE:
            QApplication::restoreOverrideCursor();
            v->setDragMode(QGraphicsView::ScrollHandDrag);
            v->setCursor(Qt::PointingHandCursor);
            QApplication::setOverrideCursor(Qt::PointingHandCursor);
            break;
    }
}

void
NMModelScene::keyPressEvent(QKeyEvent *keyEvent)
{
    if (mMode == NMS_IDLE)
    {
        if (    keyEvent->modifiers().testFlag(Qt::ShiftModifier)
             && keyEvent->modifiers().testFlag(Qt::ControlModifier)
           )
        {
            QApplication::setOverrideCursor(Qt::OpenHandCursor);
            mbIdleOpenHand = true;
        }
    }

    QGraphicsScene::keyPressEvent(keyEvent);
}

void
NMModelScene::keyReleaseEvent(QKeyEvent *keyEvent)
{
    if (mMode == NMS_IDLE && mbIdleOpenHand)
    {
        if (    !keyEvent->modifiers().testFlag(Qt::ShiftModifier)
             && !keyEvent->modifiers().testFlag(Qt::ControlModifier)
           )
        {
            QApplication::setOverrideCursor(Qt::PointingHandCursor);
            mbIdleOpenHand = false;
        }
    }

    QGraphicsScene::keyReleaseEvent(keyEvent);
}

void
NMModelScene::updateComponentItemFlags(QGraphicsItem *item)
{
    if (item == 0)
    {
        return;
    }

    switch(mMode)
    {
    case NMS_ZOOM_IN:
    case NMS_ZOOM_OUT:
    case NMS_MOVE:
    case NMS_LINK:
        item->setFlag(QGraphicsItem::ItemIsSelectable, false);
        item->setFlag(QGraphicsItem::ItemIsMovable, false);
        break;
    case NMS_SELECT:
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setFlag(QGraphicsItem::ItemIsMovable, false);
        break;

    case NMS_IDLE:
    default:
        item->setFlag(QGraphicsItem::ItemIsSelectable, false);
        item->setFlag(QGraphicsItem::ItemIsMovable, true);
    }
}

void
NMModelScene::unselectItems(void)
{
    mTempSelection.clear();
    QList<QGraphicsItem*> curSel = this->selectedItems();

    if (mMode != NMS_SELECT)
    {
        for (int i=0; i < curSel.size(); ++i)
        {
            QGraphicsItem* gi = curSel.at(i);
            gi->setSelected(false);
            gi->setFlag(QGraphicsItem::ItemIsSelectable, false);
        }
    }
    this->clearSelection();
}


void NMModelScene::setProcCompSelectability(bool selectable)
{
    QList<QGraphicsItem*> allItems = this->items();
    QListIterator<QGraphicsItem*> it(allItems);
    while(it.hasNext())
    {
        QGraphicsItem* item = it.next();
        if (item != 0)
        {
            if (!item->isSelected())
            {
                item->setFlag(QGraphicsItem::ItemIsSelectable, selectable);
            }
        }
    }
}

void NMModelScene::setLinkCompSelectability(bool selectable)
{
    QList<QGraphicsItem*> allItems = this->items();
    QListIterator<QGraphicsItem*> it(allItems);
    NMComponentLinkItem* item;
    while(it.hasNext())
    {
        item = qgraphicsitem_cast<NMComponentLinkItem*>(it.next());
        if (item != 0)
            item->setFlag(QGraphicsItem::ItemIsSelectable, selectable);
    }
}


void NMModelScene::setProcCompMoveability(bool moveable)
{
    QList<QGraphicsItem*> allItems = this->items();
    QListIterator<QGraphicsItem*> it(allItems);
    while(it.hasNext())
    {
        QGraphicsItem* ni = it.next();
        QGraphicsProxyWidget* pwi = qgraphicsitem_cast<QGraphicsProxyWidget*>(ni);
        if (pwi)
        {
            pwi->setFlag(QGraphicsItem::ItemIsSelectable, moveable);
            pwi->setFlag(QGraphicsItem::ItemIsMovable, moveable);
        }
        else
        {
            ni->setFlag(QGraphicsItem::ItemIsMovable, moveable);
        }
    }
}


QGraphicsItem*
NMModelScene::getComponentItem(const QString& name)
{
    QGraphicsItem* retItem = 0;
    QList<QGraphicsItem*> allItems = this->items();
    QListIterator<QGraphicsItem*> it(allItems);
    while(it.hasNext())
    {
        QGraphicsItem* item = it.next();
        NMProcessComponentItem* procItem = qgraphicsitem_cast<NMProcessComponentItem*>(item);
        NMAggregateComponentItem* aggrItem = qgraphicsitem_cast<NMAggregateComponentItem*>(item);
        QGraphicsProxyWidget* widgetItem = qgraphicsitem_cast<QGraphicsProxyWidget*>(item);

        if (procItem != 0)
        {
            if (procItem->getTitle().compare(name) == 0)
                return procItem;
        }
        else if (aggrItem != 0)
        {
            if (aggrItem->getTitle().compare(name) == 0)
                return aggrItem;
        }
        else if (widgetItem != 0)
        {
            if (widgetItem->objectName().compare(name) == 0)
                return widgetItem;
        }
    }

    return retItem;
}

void
NMModelScene::addParameterTable(NMSqlTableView* tv,
                                NMAggregateComponentItem* ai,
                                NMModelComponent* host)
{
    if (tv == 0)
    {
        return;
    }

    NMSqlTableModel* tabModel = qobject_cast<NMSqlTableModel*>(tv->getModel());
    QString dbFN = tabModel->getDatabaseName();

    NMModelController* controller = NMGlobalHelper::getModelController();

    NMParameterTable* pt = new NMParameterTable(host);
    pt->setLogger(controller->getLogger());
    pt->setModelController(controller);

    QStringList unquotedTabName = tv->getModel()->tableName().split('\"', Qt::SkipEmptyParts);
    pt->setTableName(unquotedTabName.at(0));
    pt->setFileName(dbFN);

    pt->setUserID(unquotedTabName.at(0));
    pt->setDescription(unquotedTabName.at(0));
    if (host == 0)
    {
        host = controller->getComponent(QString::fromLatin1("root"));
    }

    pt->setTimeLevel(host->getTimeLevel());
    QString ptName = NMGlobalHelper::getModelController()->addComponent(pt, host);


    QGraphicsProxyWidget* proxyWidget = this->addWidget(tv,
                   Qt::CustomizeWindowHint | Qt::Window | Qt::WindowTitleHint);
    proxyWidget->setObjectName(ptName);
    this->updateComponentItemFlags(proxyWidget);

//    connect(this, SIGNAL(widgetViewPortRightClicked(QGraphicsSceneMouseEvent*,QGraphicsItem*)),
//            tv, SLOT(processParaTableRightClick(QGraphicsSceneMouseEvent*,QGraphicsItem*)));
    connect(this, SIGNAL(itemDblClicked(QGraphicsSceneMouseEvent*)),
            tv, SLOT(processParaTableDblClick(QGraphicsSceneMouseEvent*)));

    pt->setTimeLevel(host->getTimeLevel());

    if (ai)
    {
        ai->addToGroup(proxyWidget);
    }
    else
    {
        this->addItem(proxyWidget);
    }
    controller->addComponent(pt, host);
    proxyWidget->setPos(proxyWidget->mapToParent(mMousePos));
}


void NMModelScene::dropEvent(QGraphicsSceneDragDropEvent* event)
{
    NMDebugCtx(ctx, << "...");
    mMousePos = event->scenePos();
//    NMLogDebug( << "drop scene pos: " << mMousePos.x() << ", " << mMousePos.y());
    if (    event->mimeData()->hasFormat("text/plain")
        ||  event->mimeData()->hasUrls())
    {
        QString dropText = event->mimeData()->text();
        QStringList dropsplit = dropText.split(':');
        QString dropSource = dropsplit.at(0);
        QString dropItem;
        if (dropsplit.count() > 1)
        {
            dropItem = dropsplit.at(1);
        }

        if (   NMGlobalHelper::getModelController()
            && NMGlobalHelper::getModelController()->isModelRunning()
           )
        {
            QMessageBox::information(0, "Invalid user request!",
                    "You cannot create new model components\nwhile a "
                    "model is being executed! Please try again later!");

            NMLogError(<< ctx << ": You cannot create new model components while there is "
                         "a model running. Please try again later!");
            NMDebugCtx(ctx, << "done!");
            return;
        }

        // do something depending on the source of the object

        // =========================================================
        //  MOVE / COPY COMPONENTS ON SCENE
        // =========================================================
        if (dropSource.startsWith(QString::fromLatin1("_NMModelScene_")))
        {
            //event->acceptProposedAction();
            QPointF dropPos = event->scenePos();
            mDropScenePos = dropPos;
            switch(event->dropAction())
            {
            case Qt::MoveAction:
                NMDebugAI( << "moving: " << mDragItemList.count() << " items" << std::endl);
//                NMLogDebug(<< mDragStartPos.x() << ", " << mDragStartPos.y()
//                           << " --> " << dropPos.x() << ", " << dropPos.y());
                emit signalItemMove(mDragItemList, mDragStartPos, dropPos);
                break;
            case Qt::CopyAction:
                NMDebugAI( << "copying: " << mDragItemList.count() << " items" << std::endl);
//                NMLogDebug(<< mDragStartPos.x() << ", " << mDragStartPos.y()
//                           << " --> " << dropPos.x() << ", " << dropPos.y());
                emit signalItemCopy(mDragItemList, mDragStartPos, dropPos);
                break;
            }
            this->mDragItemList.clear();

            // restore pointing hand cursor
            if (    QApplication::overrideCursor()
                &&  QApplication::overrideCursor()->shape() == Qt::ClosedHandCursor
               )
            {
                QApplication::restoreOverrideCursor();
            }
        }
        // =========================================================
        //  ADD NEW PROC COMP ITEMS
        // =========================================================
        else if (dropSource.startsWith(QString::fromLatin1("_NMProcCompList_")))
        {
            QGraphicsItem* pi = this->itemAt(event->scenePos(), this->views()[0]->transform());
            NMAggregateComponentItem* ai = qgraphicsitem_cast<NMAggregateComponentItem*>(pi);

            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            //  TEXT LABEL
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            if (dropItem.compare(QString::fromLatin1("TextLabel")) == 0)
            {
                QGraphicsTextItem* labelItem = new QGraphicsTextItem(ai);
                labelItem->installEventFilter(this);
                labelItem->setHtml(QString::fromLatin1("<b>Text Label</b>"));
                labelItem->setTextInteractionFlags(Qt::NoTextInteraction);
                if (ai != 0)
                {
                    ai->addToGroup(labelItem);
                    labelItem->setPos(ai->mapFromScene(event->scenePos()));
                }
                else
                {
                    this->addItem(labelItem);
                    labelItem->setPos(event->scenePos());
                }
                this->updateComponentItemFlags(labelItem);
            }
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            //  PARAMETER TABLE
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            else if (dropItem.compare(QString::fromLatin1("ParameterTable")) == 0)
            {
                NMModelComponent* host = 0;

                if (ai)
                {
                    host = NMGlobalHelper::getModelController()->getComponent(ai->getTitle());
                }
                else
                {
                    NMProcessComponentItem* procItem = qgraphicsitem_cast<NMProcessComponentItem*>(pi);
                    if (procItem)
                    {
                        host = NMGlobalHelper::getModelController()->getComponent(procItem->getTitle())->getHostComponent();
                    }
                }

                NMSqlTableView* tv = NMGlobalHelper::getMainWindow()->openCreateTable(
                            LUMASSMainWin::NM_TABVIEW_SCENE, false);

                this->addParameterTable(tv, ai, host);
            }
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            //  PROCESS COMPONENT
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            else
            {
                NMProcessComponentItem* procItem = new NMProcessComponentItem(0, this);
                procItem->setTitle(dropItem);
                procItem->setDescription(dropItem);
                procItem->setPos(event->scenePos());
                if (dropItem.compare(QString::fromLatin1("DataBuffer")) == 0
                    || dropItem.compare(QString::fromLatin1("DataBufferReference")) == 0)
                {
                    procItem->setIsDataBufferItem(true);
                }
                this->updateComponentItemFlags(procItem);

                NMDebugAI(<< "asking for creating '" << dropItem.toStdString() << "' ..." << endl);
                emit processItemCreated(procItem, dropItem, event->scenePos());
            }
        }
        // =========================================================
        //  IMPORT USER MODEL
        // =========================================================
        else if (dropSource.startsWith(QString::fromLatin1("_UserModelList_")))
        {
            QString path = NMGlobalHelper::getMainWindow()->getUserModelPath(dropItem);
            QString fileName = QString("%1/%2.lmx").arg(path).arg(dropItem);
            emit signalModelFileDropped(fileName, mMousePos);
        }
        // =========================================================
        //  IMPORT FROM FILE
        // =========================================================
        else if (event->mimeData()->hasUrls())
        {
            QGraphicsItem* item = this->itemAt(mMousePos, this->views()[0]->transform());
            NMProcessComponentItem* procItem = qgraphicsitem_cast<NMProcessComponentItem*>(item);
            NMAggregateComponentItem* aggrItem = qgraphicsitem_cast<NMAggregateComponentItem*>(item);

            // supported formats for parameter tables
            QStringList tabFormats;
            tabFormats << "dbf" << "db" << "sqlite" << "ldb" << "gpkg" << "csv" << "txt" << "xls";

            // we grab the first we can get hold of and check the ending ...
            QString fileName;
            if (event->mimeData()->urls().at(0).isLocalFile())
            {
                fileName = event->mimeData()->urls().at(0).toLocalFile();
            }

            QFileInfo fifo(fileName);
            QString suffix = fifo.suffix();

            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // LUMASS MODEL FILE
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            if (    fileName.endsWith(QStringLiteral("lmv"))
                ||  fileName.endsWith(QStringLiteral("lmx"))
                ||  fileName.endsWith(QStringLiteral("yaml"))
                ||  fileName.endsWith(QStringLiteral("yml"))
               )
            {
                if (!fileName.isEmpty())
                {
                    QFileInfo finfo(fileName);
                    if (finfo.isFile())
                    {
                        NMDebugAI(<< "gonna import model/config file: " << fileName.toStdString() << std::endl);

                        emit signalModelFileDropped(fileName, mMousePos);
                    }
                }
            }
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // PARAMETER TABLE
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            else if (   tabFormats.contains(suffix, Qt::CaseInsensitive)
                     //&& aggrItem
                    )
            {
                NMModelComponent* host = 0;
                if (aggrItem)
                {
                    host = NMGlobalHelper::getModelController()->getComponent(aggrItem->getTitle());
                }

                NMGlobalHelper h;
                LUMASSMainWin* mwin = h.getMainWindow();
                NMSqlTableView* tv = 0;
                QStringList sqliteformats;
                sqliteformats << "db" << "sqlite" << "ldb" << "gpkg";

                if (    QMessageBox::question(NMGlobalHelper::getMainWindow(),
                           QStringLiteral("Add table to model?"),
                           QStringLiteral("Do you really want to add this table to the model?"))
                     == QMessageBox::Yes
                    )
                {
                    QString tableName;
                    if (sqliteformats.contains(suffix, Qt::CaseInsensitive))
                    {
                        tableName = mwin->selectSqliteTable(fileName);
                        if (!tableName.isEmpty())
                        {
                            tv = mwin->importTable(fileName,
                                              LUMASSMainWin::NM_TABVIEW_SCENE,
                                              true,
                                              tableName);
                        }
                    }
                    else
                    {
                        tv = mwin->importTable(fileName, LUMASSMainWin::NM_TABVIEW_SCENE, true);
                    }

                    this->addParameterTable(tv, aggrItem, host);
                }
            }
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // LIST OF IMAGE/TABLE FILENAMES ON PROCESS COMP ITEM
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            else if (procItem != 0)
            {
                // check whether the procItem has got a fileName property
                NMModelComponent* comp = NMGlobalHelper::getModelController()->getComponent(procItem->getTitle());
                NMIterableComponent* itComp = qobject_cast<NMIterableComponent*>(comp);
                NMProcess* proc = 0;
                if (itComp != 0)
                {
                    proc = itComp->getProcess();
                }

                if (proc != 0)
                {
                    QStringList propList = NMGlobalHelper::getModelController()->getPropertyList(proc);
                    QStringList fnProps;
                    foreach(const QString& p, propList)
                    {
                        if (p.contains(QString::fromLatin1("FileName"), Qt::CaseInsensitive))
                        {
                            fnProps << p;
                        }
                    }


                    if (fnProps.size() > 0)
                    {
                        QStringList fileNames;
                        foreach(const QUrl& url, event->mimeData()->urls())
                        {
                                fileNames << url.toLocalFile();
                        }

                        QString theProperty = fnProps.at(0);
                        bool bOk = true;
                        if (fnProps.size() > 1)
                        {

                            theProperty = QInputDialog::getItem(0,
                                                                QString::fromLatin1("Set Filenames"),
                                                                QString::fromLatin1("Select target property:"),
                                                                fnProps, 0, false, &bOk);
                        }

                        if (bOk)
                        {
                            QVariant propVal = proc->property(theProperty.toStdString().c_str());
                            if (propVal.type() == QVariant::String)
                            {
                                // just take the first from the list
                                if (fileNames.size() > 0)
                                {
                                    QVariant strVar = QVariant::fromValue(fileNames.at(0));
                                    proc->setProperty(theProperty.toStdString().c_str(), strVar);
                                }
                            }
                            else if (propVal.type() == QVariant::StringList)
                            {
                                QStringList fnList = propVal.toStringList();
                                if (event->modifiers() & Qt::ControlModifier)
                                {
                                    fnList.append(fileNames);
                                }
                                else
                                {
                                    fnList = fileNames;
                                }

                                QVariant newVal = QVariant::fromValue(fnList);
                                proc->setProperty(theProperty.toStdString().c_str(), newVal);
                            }
                            else if (QString::fromLatin1("QList<QStringList>").compare(propVal.typeName(), Qt::CaseInsensitive) == 0)
                            {
                                QList<QStringList> fnListList = propVal.value<QList<QStringList> >();
                                fnListList.append(fileNames);

                                QVariant newVal = QVariant::fromValue(fnListList);
                                proc->setProperty(theProperty.toStdString().c_str(), newVal);
                            }
                        }
                    }
                }
            }
        }
        else
        {
            NMDebugAI(<< "No valid drag source detected!" << std::endl);
        }
    }

    //mDragItemList.clear();
    NMDebugCtx(ctx, << "done!");
}

void
NMModelScene::wheelEvent(QGraphicsSceneWheelEvent* event)
{
    mMousePos = event->scenePos();
    //qDebug() << "wheelEvent(): scene pos: " << mMousePos;
    emit zoom(event->delta());
}

void
NMModelScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    mMousePos = event->scenePos();
    if (event->button() == Qt::LeftButton)
    {
        QGraphicsItem* item = this->itemAt(event->scenePos(), this->views()[0]->transform());
        QGraphicsProxyWidget* widgetItem = qgraphicsitem_cast<QGraphicsProxyWidget*>(item);
        QGraphicsTextItem* textItem = qgraphicsitem_cast<QGraphicsTextItem*>(item);
        NMProcessComponentItem* procItem = qgraphicsitem_cast<NMProcessComponentItem*>(item);
        NMAggregateComponentItem* aggrItem = qgraphicsitem_cast<NMAggregateComponentItem*>(item);
        if (item == 0)
        {
            emit rootComponentDblClicked();
        }
        else if (widgetItem != 0)
        {
            //emit procAggregateCompDblClicked(widgetItem->objectName());
            emit itemDblClicked(event);
        }
        else if (procItem != 0)
        {
            emit procAggregateCompDblClicked(procItem->getTitle());
        }
        else if (aggrItem != 0)
        {
            emit procAggregateCompDblClicked(aggrItem->getTitle());
        }
        else if (textItem != 0)
        {
            textItem->setTextInteractionFlags(Qt::TextEditorInteraction);
        }
    }
    else
    {
        QGraphicsScene::mouseDoubleClickEvent(event);
    }
}

void
NMModelScene::serialiseItems(QList<QGraphicsItem*> items, QDataStream& data)
{

}

QGraphicsProxyWidget*
NMModelScene::getWidgetAt(const QPointF& pos)
{
    QGraphicsProxyWidget* ret = 0;
    QList<QGraphicsItem*> allItems = this->items();
    foreach (QGraphicsItem* gi, allItems)
    {
        ret = qgraphicsitem_cast<QGraphicsProxyWidget*>(gi);
        if (ret)
        {
            QRectF wf = ret->mapRectToScene(ret->windowFrameRect());
            if (wf.contains(pos))
            {
                break;
            }
            ret = 0;
        }
    }
    return ret;
}

void
NMModelScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    mMousePos = event->scenePos();
    mDragItemList.clear();
    QGraphicsItem* item = this->itemAt(event->scenePos(), this->views()[0]->transform());

    QGraphicsProxyWidget* pwi = 0;
    QGraphicsTextItem* textItem = qgraphicsitem_cast<QGraphicsTextItem*>(item);
    NMProcessComponentItem* procItem = qgraphicsitem_cast<NMProcessComponentItem*>(item);
    NMAggregateComponentItem* aggrItem = qgraphicsitem_cast<NMAggregateComponentItem*>(item);

    if (event->button() == Qt::LeftButton)
    {
        switch(mMode)
        {
        case NMS_LINK:
            mLinkLine = new QGraphicsLineItem(
                    QLineF(event->scenePos(),
                            event->scenePos()));
            mLinkLine->setPen(QPen(Qt::darkGray, 2));
            this->addItem(mLinkLine);
            break;

        case NMS_ZOOM_IN:
        case NMS_ZOOM_OUT:
        case NMS_SELECT:
            {
                mRubberBandOrigin = mMousePos;
                if (mRubberBand == 0)
                {
                    mRubberBand = new QGraphicsRectItem(0);
                    mRubberBand->setPen(QPen(Qt::darkGray, 0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    mRubberBand->setRect(mRubberBand->mapRectFromScene(QRectF(mRubberBandOrigin, QSize())));
                    this->addItem(mRubberBand);
                }

                if (mMode == NMS_SELECT)
                {
                    if (!event->modifiers().testFlag(Qt::ControlModifier))
                    {
                        this->clearSelection();
                        mTempSelection.clear();
                    }
                    else
                    {
                        mTempSelection.append(item);
                    }
                }
            }
            break;

        case NMS_MOVE:
        case NMS_IDLE:
            {
                // we insist on the left mouse button!
                if (event->button() != Qt::LeftButton)
                {
                    QGraphicsScene::mousePressEvent(event);
                    return;
                }

                mToggleSelection.clear();
                mTempSelection.clear();
                mTempSelection = this->selectedItems();
                pwi = this->getWidgetAt(event->scenePos());

                bool bNoSelection = false;
                if (mMode == NMS_MOVE)
                {
                    QApplication::setOverrideCursor(Qt::ClosedHandCursor);
                    mDragStartPos = event->screenPos();
                }
                else if (mMode == NMS_IDLE)
                {
                    // switch into move scene mode
                    if (    event->modifiers().testFlag(Qt::ShiftModifier)
                        &&  event->modifiers().testFlag(Qt::ControlModifier)
                       )
                    {
                        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
                        this->mbIdleMove = true;
                        this->setProcCompMoveability(false);
                        bNoSelection = true;
                        return;
                    }
                }

                // process selection if not moving the scene
                if (!bNoSelection)
                {
                    if (pwi)
                    {
                        if (event->modifiers().testFlag(Qt::ControlModifier))
                        {
                            mToggleSelection << pwi;
                        }

                        emit itemLeftClicked(pwi->objectName());
                    }
                    else if (item != 0)
                    {
                        if (event->modifiers().testFlag(Qt::ControlModifier))
                        {
                             mToggleSelection << item;

//                             if (textItem != nullptr)
//                             {
//                                 return;
//                             }
                        }


                    }
                }

                // process click feed-back, i.e. either show properties
                // of clicked item, or open an url from a text label
                if (procItem)
                {
                    emit itemLeftClicked(procItem->getTitle());
                }
                else if (aggrItem)
                {
                    emit itemLeftClicked(aggrItem->getTitle());
                }
//                else if (textItem)
//                {
//                    if (    event->modifiers().testFlag(Qt::ShiftModifier)
//                         && !event->modifiers().testFlag(Qt::ControlModifier)
//                       )
//                    {
//                        QUrl itemurl(textItem->toPlainText());
//                        if (itemurl.isValid())
//                        {
//                            QDesktopServices::openUrl(itemurl);
//                            return;
//                        }
//                        else
//                        {
//                            NMBoxErr2(nullptr, QString("Malfomred URL").toStdString(), itemurl.errorString().toStdString());
//                            return;
//                        }
//                    }
//                    // if we're in 'scene moving' mode, we don't want the
//                    // text label to go into edit mode, so we leave here
//                    else
//                    {
//                        textItem->setTextInteractionFlags(Qt::NoTextInteraction);
//                    }
//                }
                // nothing on the hook? honour root then!
                else if (pwi == nullptr && item == nullptr)
                {
                    emit itemLeftClicked(QString::fromUtf8("root"));
                }

                // this enables the 'built-in' handling of the
                // text label editing mode: if a text label was
                // clicked, it goes in edit mode, if another item
                // was clicked and the label was in edit mode,
                // the edit mode is stopped
                QGraphicsScene::mousePressEvent(event);
            }
            break;
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        QPointF pt = event->scenePos();
        QGraphicsItem* sendItem = 0;

        pwi = this->getWidgetAt(pt);
        if (pwi)
        {
            QRectF fr = pwi->mapRectToScene(pwi->windowFrameRect());
            QRectF cr = pwi->mapRectToScene(pwi->contentsRect());
            QRectF tb = fr.intersected(cr);
            if (!tb.contains(pt))
            {
                emit widgetTitleBarRightClicked(event, pwi);
            }
            else
            {
                //emit widgetViewPortRightClicked(event, pwi);
                NMSqlTableView* tv = qobject_cast<NMSqlTableView*>(pwi->widget());
                tv->processParaTableRightClick(event, pwi);
            }
        }
        else// if (item)
        {
            // first, we check, whether we've got a link on the hook
            sendItem = this->getLinkItem(event->scenePos());
            if (sendItem != 0) //(sendItem == 0 && item != 0)
            {
                //sendItem = item;
                item = sendItem;
            }
            //emit itemRightBtnClicked(event, sendItem);
            emit itemRightBtnClicked(event, item);
        }
    }
    else
    {
        QGraphicsScene::mousePressEvent(event);
    }
}

NMComponentLinkItem* NMModelScene::getLinkItem(QPointF pos)
{
    QGraphicsItem* item = this->itemAt(pos, this->views()[0]->transform());
    NMComponentLinkItem* link = qgraphicsitem_cast<NMComponentLinkItem*>(item);

    if (link == 0)
    {
        QPointF p = pos;
        qreal dxy = this->mLinkHitTolerance / 2.0;
        qreal wh = this->mLinkHitTolerance;
        QList<QGraphicsItem*> listItems = this->items(p.x()-dxy, p.y()-dxy, wh, wh,
                Qt::IntersectsItemShape, Qt::DescendingOrder);
        foreach(QGraphicsItem* i, listItems)
        {
            link = qgraphicsitem_cast<NMComponentLinkItem*>(i);
            if (link != 0)
            {
                NMProcessComponentItem* src = const_cast<NMProcessComponentItem*>(link->sourceItem());
                NMProcessComponentItem* tar = const_cast<NMProcessComponentItem*>(link->targetItem());
                NMDebugAI(<< "link from " << src->getTitle().toStdString()
                        << " to " << tar->getTitle().toStdString() << endl);
                break;
            }
        }
    }

    return link;
}

void
NMModelScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    // ==================================================
    // update coordinate label in status bar
    // ==================================================
    QPointF sp = event->scenePos();
    mMousePos = sp;
    QGraphicsItem* item = this->itemAt(sp, this->views()[0]->transform());
    NMAggregateComponentItem* ai = 0;
    NMProcessComponentItem* pi = 0;
    qreal x = sp.x();
    qreal y = sp.y();
    QString title = "Scene";
    if (item != 0)
    {
        pi = qgraphicsitem_cast<NMProcessComponentItem*>(item);
        if (pi != 0)
        {
            if (pi->parentItem() != 0)
            {
                ai = qgraphicsitem_cast<NMAggregateComponentItem*>(pi->parentItem());
            }
        }
        else
        {
             ai = qgraphicsitem_cast<NMAggregateComponentItem*>(item);
        }

        if (ai != 0)
        {
            title = ai->getTitle();
            QPointF aip = ai->mapFromScene(sp);
            x = aip.x();
            y = aip.y();
        }
    }

    QString pos = QString("Scene Position - X: %1 Y: %2 || %3 Position - X: %4 Y: %5")
            .arg(event->scenePos().x())
            .arg(event->scenePos().y())
            .arg(title)
            .arg(x)
            .arg(y);
    LUMASSMainWin* mainWin = NMGlobalHelper::getMainWindow();
    if (mainWin != 0)
    {
        mainWin->updateCoordLabel(pos);
    }


    // ==================================================
    //  InteractionMode control
    // ==================================================
    switch(mMode)
    {
    case NMS_LINK:
        if (mLinkLine == 0)
            break;
        mLinkLine->setLine(QLineF(mLinkLine->line().p1(), event->scenePos()));
        break;

    case NMS_ZOOM_IN:
    case NMS_ZOOM_OUT:
    case NMS_SELECT:
        {
            if (mRubberBand)
            {
                mRubberBand->setRect(mRubberBand->mapRectFromScene(
                                         QRectF(mRubberBandOrigin, event->scenePos()).normalized()));
                if (mMode == NMS_SELECT)
                {
                    QList<QGraphicsItem*> selitems = this->items(mRubberBand->sceneBoundingRect(),
                                                                 Qt::ContainsItemShape);

                    foreach(QGraphicsItem* gi, selitems)
                    {
                        gi->setSelected(true);
                    }
                }
            }
        }
        break;

    case NMS_MOVE:
        QGraphicsScene::mouseMoveEvent(event);
        break;

    case NMS_IDLE:
        {
            QGraphicsItem* dragItem = qgraphicsitem_cast<QGraphicsItem*>(this->getWidgetAt(mMousePos));
            if (dragItem == 0)
            {
                dragItem = this->itemAt(mMousePos, this->views()[0]->transform());
            }
            if (    (event->buttons() & Qt::LeftButton)
                &&  dragItem != 0
                &&  !mbIdleMove
                &&  (   QApplication::keyboardModifiers() & Qt::ControlModifier
                     || QApplication::keyboardModifiers() & Qt::ShiftModifier
                    )
               )
            {
                mDragStartPos = event->scenePos();
                mDragItemList.clear();
                mDragItemList = this->selectedItems();
                if (mDragItemList.count() == 0)
                {
                    mDragItemList.push_back(dragItem);
                }
                QRectF selRect;
                foreach(const QGraphicsItem* gi, mDragItemList)
                {
                    selRect = selRect.united(gi->mapRectToScene(gi->boundingRect()));
                }

                QGraphicsView* gv = this->views()[0];
                QPixmap dragPix = gv->grab(gv->mapFromScene(selRect).boundingRect());

                //QPixmap dragPix = QPixmap::grabWidget(this->views()[0],
                //        this->views()[0]->mapFromScene(selRect).boundingRect());
#ifdef QT_HIGHDPI_SUPPORT
    qreal pratio = NMGlobalHelper::getMainWindow()->devicePixelRatioF();
#else
    qreal pratio = 1;
#endif
                dragPix = dragPix.scaled(32*pratio, 32*pratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);

                QDrag* drag = new QDrag(this);
                QMimeData* mimeData = new QMimeData;
                QString mimeText = QString("_NMModelScene_:%1").arg(mDragItemList.count());
                mimeData->setText(mimeText);

                drag->setMimeData(mimeData);
                drag->setPixmap(dragPix);
                if (event->modifiers() & Qt::ShiftModifier)
                {
                    NMDebugAI(<< " >> moving ..." << std::endl);
                    //drag->setDragCursor(dragPix, Qt::MoveAction);
                    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
                }
                else if (event->modifiers() & Qt::ControlModifier)
                {
                   if (dragItem->type() == QGraphicsProxyWidget::Type)
                   {
                       QMessageBox::information(qobject_cast<QWidget*>(this->parent()),
                                                "Copy Item",
                                                "Sorry, LUMASS doesn't support "
                                                "(deep) copying parameter tables!");

                       //NMBoxInfo("Copy Item", "Sorry, LUMASS doesn't support (deep) copying parameter tables!");
                       return;
                   }
                   else
                   {
                       NMDebugAI(<< " >> copying ..." << std::endl);
                       //drag->setDragCursor(dragPix, Qt::CopyAction);
                       drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::CopyAction);
                   }
                }
                NMDebugAI(<< "drag start - " << mimeText.toStdString() << std::endl);
            }
        }
        this->invalidate();
        QGraphicsScene::mouseMoveEvent(event);
        break;
    }
}

void
NMModelScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    mMousePos = event->scenePos();
    NMProcessComponentItem* srcComp = 0;
    NMProcessComponentItem* tarComp = 0;
    NMComponentLinkItem* link = 0;
    QList<QGraphicsItem*> srcList;
    QList<QGraphicsItem*> tarList;

    switch(mMode)
    {
    case NMS_LINK:
        if (mLinkLine == 0)
            break;

        srcList = items(mLinkLine->line().p1());
        if (srcList.count() && srcList.first() == mLinkLine)
            srcList.removeFirst();
        tarList = items(mLinkLine->line().p2());
        if (srcList.count() && tarList.first() == mLinkLine)
            tarList.removeFirst();

        removeItem(mLinkLine);
        delete mLinkLine;
        mLinkLine = 0;

        if (srcList.count() > 0 && tarList.count() > 0 &&
            srcList.first() != tarList.first())
        {
            srcComp =
                    qgraphicsitem_cast<NMProcessComponentItem*>(srcList.first());
            tarComp =
                    qgraphicsitem_cast<NMProcessComponentItem*>(tarList.first());

            if (srcComp == 0 || tarComp == 0)
                break;
            int st = srcComp->type();
            int tt = tarComp->type();

            NMDebugAI(<< "types are: " << st << " "
                    << tt << std::endl);

            link = new NMComponentLinkItem(
                    srcComp, tarComp, 0);

            srcComp->addOutputLink(-1, link);
            tarComp->addInputLink(-1, link);
            link->setZValue(this->mLinkZLevel);
            addItem(link);
            this->invalidate();
            emit linkItemCreated(link);
        }
        break;

    case NMS_ZOOM_IN:
        {
            if (mRubberBand)
            {
                this->views().at(0)->fitInView(mRubberBand->sceneBoundingRect(),
                                               Qt::KeepAspectRatio);
                this->removeItem(mRubberBand);
                delete mRubberBand;
                mRubberBand = 0;
            }
        }
        break;

    case NMS_ZOOM_OUT:
        {
            if (mRubberBand)
            {
                QRectF rbb = mRubberBand->sceneBoundingRect();
                QWidget* vp = this->views().at(0);
                QRect vpRect(0, 0, vp->width(), vp->height());
                QRectF sr = this->views().at(0)->mapToScene(vpRect).boundingRect();

                if (rbb.width() > rbb.height())
                {
                    sr.setWidth(sr.width() * (sr.width() / rbb.width()));
                }
                else
                {
                    sr.setHeight(sr.height() * (sr.height() / rbb.height()));
                }

                sr.setTopLeft(QPointF(rbb.center().x() - (sr.width() / 2.0),
                                      rbb.center().y() - (sr.height() / 2.0)));

                this->views().at(0)->fitInView(sr, Qt::KeepAspectRatio);

                delete mRubberBand;
                mRubberBand = 0;
            }
        }
        break;

    case NMS_SELECT:
        if (mRubberBand)
        {
            QRectF delta = QRectF(mRubberBandOrigin, event->scenePos()).normalized();
            if (delta.width() < 5 && delta.height() < 5)
            {
                for (int i=0; i < mTempSelection.size(); ++i)
                {
                    if (event->modifiers() & Qt::ControlModifier)
                    {
                        mTempSelection.at(i)->setSelected(!mTempSelection.at(i)->isSelected());
                    }
                }
            }

            mTempSelection.clear();
            delete mRubberBand;
            mRubberBand = 0;
        }
        break;

    case NMS_MOVE:
    case NMS_IDLE:

        QApplication::restoreOverrideCursor();
        if (mMode == NMS_IDLE)
        {
            // states that are always clear or reverted to
            // its state prior to pressing a mouse button in
            // this mode
            if (mbIdleMove)
            {
                mbIdleMove = false;
                this->setProcCompMoveability(true);
            }

            if (    event->modifiers().testFlag(Qt::ShiftModifier)
                 && event->modifiers().testFlag(Qt::ControlModifier)
               )
            {
                QApplication::setOverrideCursor(Qt::OpenHandCursor);
            }
            else
            {
                QApplication::setOverrideCursor(Qt::PointingHandCursor);
            }

            QGraphicsItem* anitem = itemAt(event->scenePos(), this->views().at(0)->transform());
            if (anitem != nullptr)
            {
                QGraphicsTextItem* ti = qgraphicsitem_cast<QGraphicsTextItem*>(anitem);
                if (ti != nullptr)
                {
                    ti->setTextInteractionFlags(Qt::TextEditorInteraction);
                }
            }


            this->mDragItemList.clear();
        }
        else if (mMode == NMS_MOVE)
        {
            // note: here and now mDragStartPos stores the screen(!) position
            //       where the move started
            if (mDragStartPos.toPoint() == event->screenPos())
            {
                QGraphicsItem* item = itemAt(event->scenePos(), this->views().at(0)->transform());
                NMProcessComponentItem* procItem = qgraphicsitem_cast<NMProcessComponentItem*>(item);
                NMAggregateComponentItem* aggrItem = qgraphicsitem_cast<NMAggregateComponentItem*>(item);
                if (procItem)
                {
                    emit itemLeftClicked(procItem->getTitle());
                }
                else if (aggrItem)
                {
                    emit itemLeftClicked(aggrItem->getTitle());
                }
            }
        }

        // we let superclass end the default 'ScrollHandDrag' mode
        QGraphicsScene::mouseReleaseEvent(event);


        // override selection settings of the default ScrollHandDrag mode
        // depending on what which button we pressed right before and
        // whether we were pointing at any components
        for (int h=0; h < mTempSelection.size(); ++h)
        {
            mTempSelection.at(h)->setFlag(QGraphicsItem::ItemIsSelectable, true);
            mTempSelection.at(h)->setSelected(true);
        }

        foreach(QGraphicsItem* gi, mToggleSelection)
        {
            gi->setFlag(QGraphicsItem::ItemIsSelectable, !gi->isSelected());
            gi->setSelected(!gi->isSelected());
        }

        break;
    }
}

bool
NMModelScene::eventFilter(QObject *watched, QEvent *event)
{
    QGraphicsTextItem* ti = qobject_cast<QGraphicsTextItem*>(watched);
    if (ti != nullptr)
    {
        if (event->type() == QEvent::GraphicsSceneMousePress)
        {
            QGraphicsSceneMouseEvent* gsme = reinterpret_cast<QGraphicsSceneMouseEvent*>(event);

            if (    gsme->modifiers().testFlag(Qt::ShiftModifier)
                 && !gsme->modifiers().testFlag(Qt::ControlModifier)
                 && mDragTextState == 0
               )
            {
                QUrl itemurl(ti->toPlainText());
                if (itemurl.isValid())
                {
                    QDesktopServices::openUrl(itemurl);
                }
                else
                {
                    NMLogError(<< "Text label is not a valid URL!");
                }
                mDragTextState = 4;
                ti->setTextInteractionFlags(Qt::NoTextInteraction);
                return true;
            }
            else if (    !gsme->modifiers().testFlag(Qt::ShiftModifier)
                      && gsme->modifiers().testFlag(Qt::ControlModifier)
                      && mDragTextState == 0
                    )
            {
                mDragTextState = 5;
                ti->setTextInteractionFlags(Qt::NoTextInteraction);
                return true;
            }


            if (mDragTextState == 3)
            {
                mDragTextState = 3;
                return false;
            }
            else
            {
                mDragTextState = 1;
                ti->setTextInteractionFlags(Qt::NoTextInteraction);
            }
        }
        else if (event->type() == QEvent::GraphicsSceneMouseMove)
        {
            mDragTextState = 2;
        }
        else if (event->type() == QEvent::GraphicsSceneMouseRelease)
        {
            if (mDragTextState == 1)
            {
                ti->setTextInteractionFlags(Qt::TextEditorInteraction);
                ti->setFocus();
                mDragTextState = 3;
            }
            else
            {
                mDragTextState = 0;
            }
        }
    }

    return false;
}


