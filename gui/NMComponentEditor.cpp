/******************************************************************************
 * Created by Alexander Herzig
 * Copyright 2014 Landcare Research New Zealand Ltd
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

#include "NMComponentEditor.h"

#ifndef NM_ENABLE_LOGGER
#   define NM_ENABLE_LOGGER
#   include "nmlog.h"
#   undef NM_ENABLE_LOGGER
#else
#   include "nmlog.h"
#endif
#include "NMGlobalHelper.h"

#include <QObject>
#include <QMetaObject>
#include <QMetaProperty>
#include <QList>
#include <QStringList>
#include <QScopedPointer>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <limits>

#include "NMProcess.h"
#include "NMModelComponent.h"
#include "NMDataComponent.h"
#include "NMParameterTable.h"
#include "NMIterableComponent.h"
//#include "NMConditionalIterComponent.h"
#include "NMSequentialIterComponent.h"
#include "NMParallelIterComponent.h"
#include "NMHoverEdit.h"


const std::string NMComponentEditor::ctx = "NMComponentEditor";

NMComponentEditor::NMComponentEditor(QWidget *parent,
            NMCompEditorMode mode)
    : QWidget(parent), mEditorMode(mode), mObj(0), comp(0), proc(0),
      mUpdating(false)
{
    mLogger = new NMLogger(this);
    mLogger->setHtmlMode(true);

//#ifdef LUMASS_DEBUG
//    mLogger->setLogLevel(NMLogger::NM_LOG_DEBUG);
//#endif

    connect(mLogger, SIGNAL(sendLogMsg(QString)), NMGlobalHelper::getLogWidget(),
            SLOT(insertHtml(QString)));

#ifdef BUILD_RASSUPPORT
    this->mRasConn = 0;
#endif

    // set up the property browser
    switch(mEditorMode)
    {
    case NM_COMPEDITOR_GRPBOX:
        mPropBrowser = new QtGroupBoxPropertyBrowser(this);
        mPropBrowser->setObjectName("ComponentGroupBoxEditor");
        //mPropBrowser->setMinimumWidth(300);
        break;
    case NM_COMPEDITOR_TREE:
        {
            QtTreePropertyBrowser* tpb = new QtTreePropertyBrowser(this);
            tpb->setResizeMode(QtTreePropertyBrowser::Interactive);
            mPropBrowser = tpb;
            mPropBrowser->setObjectName("ComponentTreeEditor");
        }
        break;
    }

    mHoverEdit = new NMHoverEdit(this);
    mHoverEdit->setLogger(mLogger);

    connect(mHoverEdit, SIGNAL(signalCompProcChanged()),
            this, SIGNAL(signalPropertyChanged()));

    QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    //sizePolicy.setHorizontalStretch(0);
    //sizePolicy.setVerticalStretch(0);
    //sizePolicy.setHeightForWidth(mPropBrowser->sizePolicy().hasHeightForWidth());
    mPropBrowser->setSizePolicy(sizePolicy);

    QVBoxLayout* lo = new QVBoxLayout();
    lo->setSizeConstraint(QLayout::SetMinAndMaxSize);
    lo->addWidget(mPropBrowser);
    this->setLayout(lo);

    debugCounter = 1;
}

//void
//NMComponentEditor::closeEvent(QCloseEvent* event)
//{
//    emit finishedEditing(mObj);
//    QWidget::closeEvent(event);
//}

void
NMComponentEditor::update()
{
    if (mUpdating)
    {
        return;
    }

    if (mObj)
    {
        this->setObject(mObj);
    }
}

void
NMComponentEditor::signalModelConfigChanged(void)
{
    mHoverEdit->updateExpressionPreview();
}

void
NMComponentEditor::setObject(QObject* obj)
{
    //NMDebugCtx(ctx, << "...");

    // don't do anyting as long we're not ready for change!
    if (mUpdating)
    {
        return;
    }

    // reset the editor upon receiving a NULL object
    if (obj == 0)
    {
        if (this->comp != 0)
        {
            this->disconnect(comp);
        }
        if (this->proc != 0)
        {
            this->disconnect(proc);
        }

        this->mObj = 0;
        this->comp = 0;
        this->proc = 0;
        this->clear();
        debugCounter = 1;
        mHoverEdit->setProperty("", "");
        return;
    }

    NMModelComponent* c = qobject_cast<NMModelComponent*>(obj);
    NMIterableComponent* i = qobject_cast<NMIterableComponent*>(obj);
    NMProcess* p = i != 0 ? i->getProcess() : 0;

    if (mObj == 0)
    {
        if (c != 0)
        {
            mCompName = c->objectName();
            mUserID = c->getUserID();
            mObj = obj;
            comp = c;
            connect(comp, SIGNAL(nmChanged()), this, SLOT(update()));
        }
        else
            return;

        if (p != 0)
        {
            proc = p;
            connect(proc, SIGNAL(nmChanged()), this, SLOT(update()));
        }
        debugCounter = 1;
    }
    else if (mObj != 0 && obj->objectName().compare(mObj->objectName()) != 0)
    {
        if (this->comp != 0)
        {
            this->disconnect(comp);
            comp = 0;
        }
        if (this->proc != 0)
        {
            this->disconnect(proc);
            proc = 0;
        }

        if (c != 0)
        {
            mCompName = c->objectName();
            mUserID = c->getUserID();

            mObj = obj;
            comp = c;
            connect(comp, SIGNAL(nmChanged()), this, SLOT(update()));
        }
        if (p != 0)
        {
            proc = p;
            connect(proc, SIGNAL(nmChanged()), this, SLOT(update()));
        }
        debugCounter = 1;
    }

    this->readComponentProperties(mObj, comp, proc);

//      NMDebugCtx(ctx, << "done!");
}

void NMComponentEditor::clear()
{
    if (mPropBrowser)
    {
        mPropBrowser->clear();
        foreach(QtVariantPropertyManager* man, mManagers)
        {
            mPropBrowser->unsetFactoryForManager(man);
            man->clear();
            delete man;
        }
        mManagers.clear();


        foreach(QtVariantEditorFactory* fac, mFactories)
        {
            delete fac;
        }
        mFactories.clear();
    }
}

void NMComponentEditor::readComponentProperties(QObject* obj, NMModelComponent* comp,
        NMProcess* proc)
{
    this->clear();

    //NMDebugAI(<< ">>>> #" << debugCounter << " - START: " << mObj->objectName().toStdString() << " >>>>>>>>>>>>>>>>>>" << std::endl);

    // let's start with the component's properties,
    // in case we've got a component
    if (comp != 0)// && compq->getProcess() == 0)
    {
        //connect(comp, SIGNAL(nmChanged()), this, SLOT(update()));

        // ------------------------------------------------------
        // PROCESSING proper NMModelComponent and subclasses' PROPERTIES
        // ------------------------------------------------------
        NMParameterTable* pto = qobject_cast<NMParameterTable*>(comp);
        QStringList ptoNoProps;
        if (pto != nullptr)
        {
            ptoNoProps << "Description" << "Inputs" << "IsStreamable";
        }

        const QMetaObject* meta = obj->metaObject();
        unsigned int nprop = meta->propertyCount();
        for (unsigned int p=0; p < nprop; ++p)
        {
            QMetaProperty property = meta->property(p);
            if (     QString(property.name()).endsWith("Enum")
                 ||  ptoNoProps.contains(property.name())
               )
            {
                continue;
            }
            this->createPropertyEdit(property, obj);
        }

        // ------------------------------------------------------
        // PROCESSING (im)proper AGGREGATE COMPONENT 'PROPERTIES'
        // ------------------------------------------------------

        // do we have a process component?
        NMIterableComponent* procComp =
                qobject_cast<NMIterableComponent*>(comp);
        if (procComp != 0)
            proc = procComp->getProcess();
        else
            proc = 0;

        if (procComp != 0 && proc == 0)
        {
            // now we add the subcomponents list for reference
            QStringList strCompChain;

            NMModelComponentIterator cit = procComp->getComponentIterator();
            strCompChain << "{";
            while(!cit.isAtEnd())
            {
                strCompChain << QString(tr("{%1}")).arg(cit->objectName());
                ++cit;
            }
            strCompChain << "}";

            QtVariantEditorFactory* ed = new QtVariantEditorFactory();
            mFactories.append(ed);
            QtVariantPropertyManager* man = new QtVariantPropertyManager();
            mManagers.append(man);
            QtVariantProperty* vprop;

            //connect(man, SIGNAL(valueChanged(QtProperty*, const QVariant &)),
            //        this, SLOT(applySettings(QtProperty*, const QVariant &)));

            QVariant vval = QVariant::fromValue(strCompChain);
            vprop = man->addProperty(QVariant::StringList, "Subcomponents");
            vprop->setValue(vval);
            vprop->setEnabled(false);
            mPropBrowser->setFactoryForManager(man, ed);
            mPropBrowser->addProperty(vprop);
        }
    }

    // ------------------------------------------------------
    // PROCESSING A PROCESS COMPONENT
    // ------------------------------------------------------

    // if this is a process component, we add the process'
    // properties to the dialog
    if (proc != 0)
    {
        //connect(proc, SIGNAL(nmChanged()), this, SLOT(update()));

        const QMetaObject* procmeta = proc->metaObject();
        unsigned int nprop = procmeta->propertyCount();
        for (unsigned int p=0; p < nprop; ++p)
        {
            QMetaProperty property = procmeta->property(p);
            if (    QString(property.name()).endsWith("Enum")
                 || QString::fromLatin1("InputComponents").compare(QString(property.name())) == 0
                 || QString::fromLatin1("ParameterHandling").compare(QString(property.name())) == 0
                 || (   proc->getUserProperty(property.name()).isEmpty()
                     && QString(property.name()).compare(QStringLiteral("objectName")) != 0
                    )
               )
            {
                continue;
            }
            this->createPropertyEdit(property, proc);
        }
    }

    //NMDebugAI(<< "<<<< #" << debugCounter << " - END: " << mObj->objectName().toStdString() << " <<<<<<<<<<<<<<<<<<" << std::endl);
    debugCounter++;
}

void NMComponentEditor::createPropertyEdit(const QMetaProperty& property,
        QObject* obj)
{
    NMModelComponent* comp = qobject_cast<NMModelComponent*>(obj);
    NMProcess* proc = qobject_cast<NMProcess*>(obj);

    QString propName = QString(property.name());
    QString displayName = propName;

    if (proc != nullptr)
    {
        displayName = proc->getUserProperty(propName);
    }

    if (QString("objectName").compare(property.name()) == 0)
    {
        if (proc != 0)
        {
            //propName = "ProcessName";
            displayName = "ProcessName";
        }
        else if (comp != 0)
        {
            //propName = "ComponentName";
            displayName = "ComponentName";
        }
    }
    int propType = property.userType();

//    NMDebugAI(<< propName.toStdString() << " (" << property.typeName()
//            << "): " << obj->property(property.name()).toString().toStdString()
//            << " ...");

    QString propToolTip = "";
    QVariant value;
    bool bok;
    QStringList ctypes;
    ctypes << "uchar" << "char" << "ushort" << "short"
           << "uint" << "int" << "ulong" << "long"
           << "ulonglong" << "longlong" << "float" << "double" << "unknown";

    QStringList parammodes;
    parammodes << "NM_USE_UP" << "NM_CYCLE" << "NM_SYNC_WITH_HOST";

    QtVariantPropertyManager* manager = new QtVariantPropertyManager();
    mManagers.append(manager);
    QtVariantProperty* prop = 0;

    // assign replacement types for the not supported variant
    // types
    if (QString("char").compare(property.typeName()) == 0 ||
        QString("int").compare(property.typeName()) == 0 ||
        QString("short").compare(property.typeName()) == 0 ||
        QString("long").compare(property.typeName()) == 0
       )
    {
        propType = QVariant::Int;
        value = obj->property(property.name()).toInt(&bok);
        prop = manager->addProperty(propType, displayName);
    }
    else if (QString("uchar").compare(property.typeName()) == 0 ||
            QString("uint").compare(property.typeName()) == 0 ||
            QString("ushort").compare(property.typeName()) == 0 ||
            QString("ulong").compare(property.typeName()) == 0
           )
    {
        value = obj->property(property.name()).toInt(&bok);
        propType = value.type();
        prop = manager->addProperty(propType, displayName);
        prop->setAttribute("minimum", 1);
        prop->setAttribute("maximum", std::numeric_limits<int>::max());
    }
    else if (QString("float").compare(property.typeName()) == 0)
    {
        value = obj->property(property.name()).toDouble(&bok);
        propType = value.type();
        prop = manager->addProperty(propType, displayName);
        prop->setAttribute("minimum", QVariant::fromValue(
                (std::numeric_limits<float>::max()-1) * -1));
        prop->setAttribute("maximum", QVariant::fromValue(
                std::numeric_limits<float>::max()));
    }
    else if (QString("NMItkDataObjectWrapper::NMComponentType")
            .compare(property.typeName()) == 0)
    {
        propType = QtVariantPropertyManager::enumTypeId();
        prop = manager->addProperty(propType, displayName);
        prop->setAttribute("enumNames", ctypes);
        QString curPropValStr = NMItkDataObjectWrapper::getComponentTypeString(
                obj->property(property.name())
                .value<NMItkDataObjectWrapper::NMComponentType>());
//        NMDebug(<< "current value = " << curPropValStr.toStdString());
        for (unsigned int p=0; p < ctypes.size(); ++p)
        {
            if (ctypes.at(p).compare(curPropValStr) == 0)
                value = QVariant(p);
        }
        propToolTip = tr("PixelType");
    }
    else if (QString("NMProcess::AdvanceParameter").compare(property.typeName()) == 0)
    {
        propType = QtVariantPropertyManager::enumTypeId();
        prop = manager->addProperty(propType, displayName);
        prop->setAttribute("enumNames", parammodes);
        NMProcess::AdvanceParameter ap =
                obj->property(property.name()).value<NMProcess::AdvanceParameter>();
        value = QVariant(ap);
        propToolTip = tr("NMProcess::AdvanceParameter");
//        NMDebug(<< "current value = " << ap);
    }
#ifdef BUILD_RASSUPPORT
    else if (QString("NMRasdamanConnectorWrapper*")
            .compare(property.typeName()) == 0)
    {
        NMRasdamanConnectorWrapper* wrap = obj->property(property.name())
                .value<NMRasdamanConnectorWrapper*>();

        propType = QVariant::Bool;
        value = QVariant(false);
        prop = manager->addProperty(propType, displayName);

        if (this->mRasConn == 0)
        {
            prop->setEnabled(false);
        }
        else if (this->mRasConn->getConnector() == 0)
        {
            prop->setEnabled(false);
        }

        if (wrap != 0 && wrap->getConnector() != 0)
        {
            value = QVariant(true);
        }
    }
#endif
    else if (QString("QStringList")
            .compare(property.typeName()) == 0)
    {
        QStringList nakedList = obj->property(property.name()).toStringList();
        QStringList bracedList;
        bracedList << QString("{");
        foreach(QString s, nakedList)
        {
            bracedList << QString(tr("{%1}").arg(s));
        }
        bracedList << QString(tr("}"));

        value = QVariant::fromValue(bracedList);
        prop = manager->addProperty(QVariant::StringList, displayName);
        propToolTip = tr("QStringList");
    }
    else if (QString("QList<QStringList>").compare(property.typeName()) == 0)
    {
        QList<QStringList> lst;
        QStringList bracedList;
        if (obj->property(property.name()).canConvert(QMetaType::type("QList<QStringList>")))
        {
            lst = obj->property(property.name()).value<QList<QStringList> >();
            bracedList << QString("{");
        }
        else
        {
            bracedList << QString::fromLatin1("invalid expression: {");
        }

        foreach(QStringList l, lst)
        {
            bracedList << QString("{");
            foreach(QString s, l)
            {
                bracedList << QString(tr("{%1}").arg(s));
            }
            bracedList << QString(tr("}"));
        }
        bracedList << QString("}");

        value = QVariant::fromValue(bracedList);
        prop = manager->addProperty(QVariant::StringList, displayName);
        propToolTip = tr("QList<QStringList>");
    }
    else if (QString("QList<QList<QStringList> >").compare(property.typeName()) == 0)
    {
        QList<QList<QStringList> > llsl = obj->property(property.name()).value<QList<QList<QStringList> > >();
        QStringList bracedList;

        bracedList << QString("{");
        foreach(QList<QStringList> lsl, llsl)
        {
            bracedList << QString("{");
            foreach(QStringList sl, lsl)
            {
                bracedList << QString("{");
                foreach(QString s, sl)
                {
                    bracedList << QString(tr("{%1}").arg(s));
                }
                bracedList << QString(tr("}"));
            }
            bracedList << QString("}");
        }
        bracedList << QString("}");

        value = QVariant::fromValue(bracedList);
        prop = manager->addProperty(QVariant::StringList, displayName);
        propToolTip = tr("QList<QList<QStringList> >");
    }
    else if (QString("QString").compare(property.typeName()) == 0
             && propName.endsWith("Type")
             && obj->property(QString("%1Enum")
                              .arg(propName.left(propName.size()-4)
                                   ).toStdString().c_str())
                != QVariant::Invalid)
    {
        // let's see whether we've got Type/Enum property pair
        //QString tname(property.name());
        QString tname(propName);
        QString enumProp = QString("%1Enum").arg(tname.left(tname.size()-4));
        QStringList typeList = obj->property(enumProp.toStdString().c_str()).toStringList();

        propType = QtVariantPropertyManager::enumTypeId();
        prop = manager->addProperty(propType, displayName);
        prop->setAttribute("enumNames", typeList);

        QString stype = obj->property(property.name()).toString();
        for (int i=0; i < typeList.size(); ++i)
        {
            if (stype.compare(typeList.at(i)) == 0)
            {
                value = QVariant::fromValue(i);
                break;
            }
        }
    }
    else
    {
//        NMDebug(<< "standard ");
        value = obj->property(property.name());
        prop = manager->addProperty(propType, displayName);

        NMParameterTable* pto = qobject_cast<NMParameterTable*>(obj);
        if (    pto != nullptr
             && (    QString("TableName").compare(property.name()) == 0
                  || QString("FileName").compare(property.name()) == 0
                )
           )
        {
            prop->setEnabled(false);
        }
    }

    // add property to browser and set value
    if (prop != 0)
    {
        QtVariantEditorFactory* editor = new QtVariantEditorFactory();
        mFactories.append(editor);

        if (!value.isNull())
            prop->setValue(value);

        if (prop->propertyName().compare("ProcessName") == 0 ||
            prop->propertyName().compare("ComponentName") == 0)
            prop->setEnabled(false);

        if (propToolTip.isEmpty())
            propToolTip = value.typeName();

        //prop->setToolTip(QString(tr("type: %1")).arg(propToolTip));
        //prop->setToolTip(QString(tr("type: %1")).arg(value.typeName()));
        prop->setToolTip(propToolTip);

        mPropBrowser->setFactoryForManager(manager, editor);
        mPropBrowser->addProperty(prop);

        connect(manager, SIGNAL(valueChanged(QtProperty*, const QVariant &)),
                this, SLOT(applySettings(QtProperty*, const QVariant &)));

        connect(manager, SIGNAL(signalCallAuxEditor(QtProperty*, const QStringList &)),
                 this, SLOT(callFeeder(QtProperty*, const QStringList &)));

        //        NMDebug(<< " - processed!" << std::endl);
    }
    else
    {
        //        NMDebug(<< " - failed!" << std::endl);
        delete manager;
    }
}

void
NMComponentEditor::callFeeder(QtProperty* prop, const QStringList& val)
{
    mHoverEdit->setProperty(mCompName, prop->propertyName());
    mHoverEdit->show();
}

void NMComponentEditor::applySettings(QtProperty* prop,
        const QVariant& val)
{
    NMDebugCtx(ctx, << "...");

    if (comp == 0 && proc == 0)
    {
        NMDebugCtx(ctx, << "done!");
        return;
    }

    //NMModelComponent* comp = qobject_cast<NMModelComponent*>(mObj);
    NMIterableComponent* itComp = qobject_cast<NMIterableComponent*>(mObj);
    NMProcess* proc = nullptr;
    if (itComp != nullptr && itComp->getProcess() != nullptr)
    {
        proc = itComp->getProcess();
    }

    QString propName = prop->propertyName();

    // for NMProcess objects we need to look up the proper
    // property name associated with the displayed name
    if (proc != nullptr)
    {
        propName = proc->mapDisplayToPropertyName(propName);
    }

    mUpdating = true;
    if (mObj->property(propName.toStdString().c_str()).isValid() ||
        (propName.compare("ComponentName")== 0))
    {
        this->setComponentProperty(prop, mObj);
    }
    else if (itComp != 0 && itComp->getProcess() != 0)
    {
        proc = itComp->getProcess();
        if (proc->property(propName.toStdString().c_str()).isValid() ||
            (propName.compare("ProcessName") == 0))
        {
            this->setComponentProperty(prop, proc);
        }
    }
    else // ??
    {
        this->setComponentProperty(prop, mObj);
    }

    emit signalPropertyChanged();
    mUpdating = false;
    NMDebugCtx(ctx, << "done!");
}

void NMComponentEditor::setComponentProperty(const QtProperty* prop,
        QObject* obj)
{
    bool bok;
    QStringList ctypes;
    ctypes << "uchar" << "char" << "ushort" << "short"
           << "uint" << "int" << "ulong" << "long"
           << "ulonglong" << "longlong" << "float" << "double" << "unknown";

    QStringList parammodes;
    parammodes << "NM_USE_UP" << "NM_CYCLE" << "NM_SYNC_WITH_HOST";

    QtVariantPropertyManager* manager =
            qobject_cast<QtVariantPropertyManager*>(prop->propertyManager());
    if (manager == 0)
    {
        NMLogError(<< ctx << ": couldn't get the property manager for "
                << prop->propertyName().toStdString() << "!");
        return;
    }
    QVariant value = manager->value(prop);

    NMDebugAI(<< "setting " << prop->propertyName().toStdString()
            << " (" << value.typeName() << " = " << value.toString().toStdString()
            << ") ... ");

    QVariant updatedValue;

    QString propName = prop->propertyName();

    // if we set the property for an NMProcess object, we need  to
    // identify the proper Property name first
    NMProcess* proc = qobject_cast<NMProcess*>(obj);
    if (proc != nullptr)
    {
        propName = proc->mapDisplayToPropertyName(propName);
    }

    if (QString("ComponentName").compare(propName) == 0 ||
        QString("ProcessName").compare(propName) == 0)
    {
        propName = "objectName";
        updatedValue = value.toString();
    }
#ifdef BUILD_RASSUPPORT
    else if (QString("RasConnector").compare(propName) == 0)
    {
        if (value.toBool())
        {
            if (this->mRasConn == 0)
            {
                NMLogError(<< ctx << ": rasdaman connector requested, but non available!");
                updatedValue.setValue<NMRasdamanConnectorWrapper*>(0);
            }
            else if (this->mRasConn->getConnector() == 0)
            {
                NMLogError( << ctx << ":rasdaman connector requested, but non available!");
                updatedValue.setValue<NMRasdamanConnectorWrapper*>(0);
            }
            else
                updatedValue.setValue<NMRasdamanConnectorWrapper*>(this->mRasConn);
        }
        else
            updatedValue.setValue<NMRasdamanConnectorWrapper*>(0);
    }
#endif
    else if (QString("int").compare(value.typeName()) == 0 &&
            propName.contains("ComponentType"))
    {
        NMItkDataObjectWrapper::NMComponentType type =
                NMItkDataObjectWrapper::getComponentTypeFromString(ctypes.at(value.toInt(&bok)));
        updatedValue = QVariant::fromValue(type);
    }
    else if (QString("ParameterHandling").compare(propName) == 0)
    {
        int v = value.toInt(&bok);
        NMProcess::AdvanceParameter ap;
        switch(v)
        {
            case 0: ap = NMProcess::NM_USE_UP; break;
            case 1: ap = NMProcess::NM_CYCLE; break;
            case 2: ap = NMProcess::NM_SYNC_WITH_HOST; break;
        }
        updatedValue.setValue<NMProcess::AdvanceParameter>(ap);// = QVariant::fromValue(ap);
    }
    else if (QString("QStringList").compare(value.typeName()) == 0)
    {
        NMDebugAI(<< "incoming stringlist: " << value.toStringList().join(" | ").toStdString() << std::endl);
        QStringList bracedList = value.toStringList();
        bool bvalid = false;
        if (!bracedList.isEmpty())
        {
            if (!bracedList.at(0).isEmpty() && !bracedList.at(0).startsWith("invalid"))
            {
                updatedValue = this->nestedListFromStringList(bracedList);
                bvalid = true;
            }
        }

        // we set an empty value (i.e. override current property set for this object),
        // if the incoming value is not valid; otherwise, no opportunity for the user
        // to remove/clear a once set value, e.g. as it would be the case when a model
        // is loaded from disk.
        if (!bvalid)
        {
           QVariant emptyValue;
           QString longtypename = obj->property(propName.toStdString().c_str()).typeName();
           if (QString("QStringList").compare(longtypename) == 0)
           {
                emptyValue = QVariant::fromValue(QStringList());
           }
           else if (QString("QList<QStringList>").compare(longtypename) == 0)
           {
               emptyValue = QVariant::fromValue(QList<QStringList>());
           }
           else if (QString("QList<QList<QStringList> >").compare(longtypename) == 0)
           {
               emptyValue = QVariant::fromValue(QList< QList<QStringList> >());
           }

           updatedValue = emptyValue;
        }
    }
    else if (    propName.endsWith("Type")
             &&  obj->property(QString("%1Enum")
                      .arg(propName.left(propName.size()-4))
                      .toStdString().c_str())
                 != QVariant::Invalid
            )
    {
        QString enumProp = QString("%1Enum").arg(propName.left(propName.size()-4));
        QStringList typeList = obj->property(enumProp.toStdString().c_str()).toStringList();

        updatedValue = QVariant::fromValue(typeList.at(value.toInt()));
    }
    else
    {
        updatedValue = value;
    }

    // do some value checking and set the new value
    if (!updatedValue.isNull() && updatedValue.isValid())
    {
        if (QString("Subcomponents").compare(propName) == 0 &&
            QString("QStringList").compare(updatedValue.typeName()) == 0)
        {
            this->updateSubComponents(updatedValue.value<QStringList>());
        }
        else
        {
            obj->setProperty(propName.toStdString().c_str(), updatedValue);
            NMDebugAI(<< "object property updated - type '"
                    << updatedValue.typeName() << "'" << std::endl);
        }
    }
    else
    {
        NMDebugAI(<< "object property update failed! Invalid value!" << std::endl);
    }
}

void NMComponentEditor::updateSubComponents(const QStringList& compList)
{
    NMIterableComponent* comp = qobject_cast<NMIterableComponent*>(mObj);
    if (comp == 0)
    {
        NMWarn(ctx, << "comp is NULL!");
        return;
    }

    QStringList oldCompNames;
    QList<NMModelComponent*> oldComps;

    NMModelComponentIterator cit = comp->getComponentIterator();
    while(!cit.isAtEnd())
    {
        oldCompNames.push_back(cit->objectName());
        ++cit;
    }

    // remove all components
    // and add store them in a list
    foreach(QString ds, oldCompNames)
    {
        oldComps.append(comp->removeModelComponent(ds));
    }

    // add components in new order, make sure we don't add any new
    // components
    foreach(QString us, compList)
    {
        int idx = oldCompNames.indexOf(us);
        if(idx >=0)
        {
            comp->addModelComponent(oldComps.value(idx));
        }
    }
}


QVariant
NMComponentEditor::nestedListFromStringList(const QStringList& strList)
{
    //NMDebugCtx(ctx, << "...");
    QVariant val;

    // determine the max depth (level) of the stringlist
    int allowedlevel = 3;
    int maxlevel = 0;
    int levelcounter = 0;
    foreach(QString ts, strList)
    {
        ts = ts.trimmed();
        if (ts.compare("{") == 0)// || ts.startsWith("{"))
        {
            ++levelcounter;
            maxlevel = levelcounter > maxlevel && levelcounter <= allowedlevel ? levelcounter : maxlevel;
        }
        else if (ts.compare("}") == 0)// || ts.endsWith("}"))
        {
            --levelcounter;
        }
    }
    //NMDebugAI( << "detected maxlevel: " << maxlevel << std::endl);
    //NMDebugAI( << "parsing list ..." << std::endl << std::endl);

    QList<QList<QStringList> >* llsl;
    QList<QStringList>* lsl;
    QStringList* sl;

    levelcounter = maxlevel;
    foreach(QString ts, strList)
    {
        ts = ts.trimmed();
//		NMDebugAI(<< "'" << ts.toStdString() << "' on level " << levelcounter);
        if (ts.compare("{") == 0)
        {
            switch (levelcounter)
            {
            case 3:
//				NMDebug(<< " -> new llsl" << std::endl);
                llsl = new QList<QList<QStringList> >();
                break;
            case 2:
//				NMDebug(<< " -> new lsl" << std::endl);
                lsl = new QList<QStringList>();
                break;
            case 1:
//				NMDebug(<< " -> new sl" << std::endl);
                sl = new QStringList();
                break;
            default:
//				NMDebug(<< " - > can't handle this one!" << std::endl);
                continue;
                break;
            }
            --levelcounter;
        }
        else if (ts.compare("}") == 0)
        {
            switch (levelcounter)
            {
            case 0:
                if (maxlevel > 1)
                {
//					NMDebug(<< " -> close ")
                    lsl->push_back(*sl);
                    sl->clear();
                    delete sl;
                }
                break;
            case 1:
                if (maxlevel > 2)
                {
                    llsl->push_back(*lsl);
                    lsl->clear();
                    delete lsl;
                }
                break;
            default:
                break;
            }
            ++levelcounter;
        }
        else if (ts.startsWith("{")
                 && ts.endsWith("}")
                   && levelcounter == 0)
        {
            QString ns = ts.mid(1,ts.size()-2); //ts.remove(0,1);
            sl->push_back(ns);
//			NMDebugAI( << ">>> pushed '" << ns.toStdString() << "'" << std::endl);
        }
    }

    switch(maxlevel)
    {
    case 3:
        val = QVariant::fromValue(*llsl);
        llsl->clear();
        delete llsl;
        break;
    case 2:
        val = QVariant::fromValue(*lsl);
        lsl->clear();
        delete lsl;
        break;
    case 1:
        val = QVariant::fromValue(*sl);
        sl->clear();
        delete sl;
        break;
    default:
        break;
    }

    //NMDebugCtx(ctx, << "done!");
    return val;
}


