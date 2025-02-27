/******************************************************************************
 * Created by Alexander Herzig
 * Copyright 2016 Landcare Research New Zealand Ltd
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
 *  NMScriptableKernelFilter2Wrapper.cpp
 *
 *  Created on: 2016-06-23
 *      Author: Alexander Herzig
 */

#include <algorithm>

#include "NMScriptableKernelFilter2Wrapper.h"
#include "nmlog.h"

#include "NMMacros.h"
#include "NMMfwException.h"
#include "NMModelController.h"

#include "itkProcessObject.h"
#include "otbImage.h"
#include "otbNMScriptableKernelFilter2.h"

/*! Internal templated helper class linking to the core otb/itk filter
 *  by static methods.
 */
template<class TInputImage, class TOutputImage, unsigned int Dimension>
class NMScriptableKernelFilter2Wrapper_Internal
{
public:
    typedef otb::Image<TInputImage, Dimension>  InImgType;
    typedef otb::Image<TOutputImage, Dimension> OutImgType;
    typedef typename otb::NMScriptableKernelFilter2<InImgType, OutImgType>      FilterType;
    typedef typename FilterType::Pointer        FilterTypePointer;

    // more typedefs
    typedef typename InImgType::PixelType  InImgPixelType;
    typedef typename OutImgType::PixelType OutImgPixelType;

    typedef typename OutImgType::SpacingType      OutSpacingType;
    typedef typename OutImgType::SpacingValueType OutSpacingValueType;
    typedef typename OutImgType::PointType        OutPointType;
    typedef typename OutImgType::PointValueType   OutPointValueType;
    typedef typename OutImgType::SizeValueType    SizeValueType;

    static void createInstance(itk::ProcessObject::Pointer& otbFilter,
            unsigned int numBands)
    {
        FilterTypePointer f = FilterType::New();
        otbFilter = f;
    }

    static void setNthInput(itk::ProcessObject::Pointer& otbFilter,
                    unsigned int numBands, unsigned int idx, itk::DataObject* dataObj, const QString& name)
    {
        //InImgType* img = dynamic_cast<InImgType*>(dataObj);
        FilterType* filter = dynamic_cast<FilterType*>(otbFilter.GetPointer());
        //filter->SetFilterInput(idx, dataObj);
        filter->SetNthInput(idx, dataObj);
    }


    static itk::DataObject* getOutput(itk::ProcessObject::Pointer& otbFilter,
            unsigned int numBands, unsigned int idx)
    {
        FilterType* filter = dynamic_cast<FilterType*>(otbFilter.GetPointer());
        return dynamic_cast<OutImgType*>(filter->GetOutput(idx));
    }

/*$<InternalRATGetSupport>$*/

    static void setRAT(
        itk::ProcessObject::Pointer& procObj,
        unsigned int numBands, unsigned int idx,
        otb::AttributeTable::Pointer& rat)
    {
        FilterType *f = dynamic_cast<FilterType*>(procObj.GetPointer());
        return f->setRAT(idx, rat);
    }



    static void internalLinkParameters(itk::ProcessObject::Pointer& otbFilter,
            unsigned int numBands, NMProcess* proc,
            unsigned int step, const QMap<QString, NMModelComponent*>& repo)
    {
        NMDebugCtx("NMScriptableKernelFilter2Wrapper_Internal", << "...");

        FilterType* f = dynamic_cast<FilterType*>(otbFilter.GetPointer());
        NMScriptableKernelFilter2Wrapper* p =
                dynamic_cast<NMScriptableKernelFilter2Wrapper*>(proc);

        // make sure we've got a valid filter object
        if (f == 0)
        {
            NMMfwException e(NMMfwException::NMProcess_UninitialisedProcessObject);
            e.setSource(p->parent()->objectName().toStdString());
            e.setDescription("We're trying to link, but the filter doesn't seem to be initialised properly!");
            throw e;
            return;
        }

        /* do something reasonable here */
        bool bok;
        int givenStep = step;

        f->SetNumThreads(p->getNumThreads());


        QVariant curRadiusVar = p->getParameter("Radius");
        if (curRadiusVar.isValid())
        {
           std::vector<int> vecRadius;
           QStringList curValVarList = curRadiusVar.toStringList();
           foreach(const QString& vStr, curValVarList)
           {
                int curRadius = vStr.toInt(&bok);
                if (bok)
                {
                    vecRadius.push_back(static_cast<int>(curRadius));
                }
                else
                {
                    NMLogError(<< "NMScriptableKernelFilter2Wrapper_Internal: " << "Invalid value for 'Radius'!");
                    NMMfwException e(NMMfwException::NMProcess_InvalidParameter);
                    e.setSource(p->parent()->objectName().toStdString());
                    e.setDescription("Invalid value for 'Radius'!");
                    throw e;
                }
            }
            if (vecRadius.size() > 0)
            {
                f->SetRadius(static_cast<int*>(&vecRadius[0]));
            }
            else
            {
                f->SetRadius(0);
            }
            QString radiusVarProvN = QString("nm:Radius=\"%1\"").arg(curValVarList.join(' '));
            p->addRunTimeParaProvN(radiusVarProvN);
        }

        QVariant curKernelScriptVar = p->getParameter("KernelScript");
        std::string curKernelScript;
        if (curKernelScriptVar.isValid())
        {
            curKernelScript = curKernelScriptVar.toString().simplified().toStdString();
            f->SetKernelScript(curKernelScript);
            QString kernelScriptProvN = QString("nm:KernelScript=\"%1\"").arg(curKernelScript.c_str());
            p->addRunTimeParaProvN(kernelScriptProvN);
        }

        QVariant curInitScriptVar = p->getParameter("InitScript");
        std::string curInitScript;
        if (curInitScriptVar.isValid())
        {
            curInitScript = curInitScriptVar.toString().simplified().toStdString();
            f->SetInitScript(curInitScript);
            QString initScriptProvN = QString("nm:InitScript=\"%1\"").arg(curInitScript.c_str());
            p->addRunTimeParaProvN(initScriptProvN);
        }

        QVariant curKernelShapeVar = p->getParameter("KernelShapeType");
        std::string curKernelShape;
        if (curKernelShapeVar.isValid())
        {
            curKernelShape = curKernelShapeVar.toString().toStdString();
            f->SetKernelShape(curKernelShape);
            QString kernelShapeProvN = QString("nm:KernelShapeType=\"%1\"").arg(curKernelShape.c_str());
            p->addRunTimeParaProvN(kernelShapeProvN);
        }

        QVariant curOutputVarNameVar = p->getParameter("OutputVarName");
        std::string curOutputVarName;
        if (curOutputVarNameVar.isValid())
        {
            curOutputVarName = curOutputVarNameVar.toString().simplified().toStdString();
            f->SetOutputVarName(curOutputVarName);
            QString outputVarNameProvN = QString("nm:OutputVarName=\"%1\"").arg(curOutputVarName.c_str());
            p->addRunTimeParaProvN(outputVarNameProvN);
        }

        QVariant curNodataVar = p->getParameter("Nodata");
        double curNodata;
        if (curNodataVar.isValid())
        {
            curNodata = curNodataVar.toDouble(&bok);
            if (bok)
            {
                f->SetNodata((curNodata));
            }
            else
            {
                NMLogError(<< "NMScriptableKernelFilter2Wrapper_Internal: " << "Invalid value for 'Nodata'!");
                NMMfwException e(NMMfwException::NMProcess_InvalidParameter);
                e.setSource(p->parent()->objectName().toStdString());
                e.setDescription("Invalid value for 'Nodata'!");
                throw e;
            }
            QString curNodataProvN = QString("nm:Nodata=\"%1\"").arg(curNodataVar.toString());
            p->addRunTimeParaProvN(curNodataProvN);
        }



        step = p->mapHostIndexToPolicyIndex(givenStep, p->mInputComponents.size());
        std::vector<std::string> userIDs;
        QStringList currentInputs;
        QStringList inputNamesProvVal;
        if (step < p->mInputComponents.size())
        {
            currentInputs = p->mInputComponents.at(step);
            int cnt=0;
            foreach (const QString& input, currentInputs)
            {
                std::stringstream uid;
                uid << "L" << cnt;
                QString inputCompName = p->getModelController()->getComponentNameFromInputSpec(input);
                NMModelComponent* comp = p->getModelController()->getComponent(inputCompName);
                if (comp != 0)
                {
                    if (comp->getUserID().isEmpty())
                    {
                        userIDs.push_back(uid.str());
                    }
                    else
                    {
                        userIDs.push_back(comp->getUserID().toStdString());
                    }
                }
                else
                {
                    userIDs.push_back(uid.str());
                }
                inputNamesProvVal << QString(userIDs.back().c_str());
                ++cnt;
            }
        }
        f->SetInputNames(userIDs);
        QString inputNamesProvN = QString("nm:InputNames=\"%1\"").arg(inputNamesProvVal.join(' '));
        p->addRunTimeParaProvN(inputNamesProvN);


        // set the workspace path
        f->SetWorkspacePath(p->getModelController()->getSetting("Workspace").toString().toStdString());



        NMDebugCtx("NMScriptableKernelFilter2Wrapper_Internal", << "done!");
    }
};

template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, char, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, short, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, int, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, long, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, float, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, double, 1>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, char, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, short, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, int, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, long, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, float, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, double, 2>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned char, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<char, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned short, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<short, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned int, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<int, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<unsigned long, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<long, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<float, double, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, char, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, short, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, int, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, unsigned long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, long, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, float, 3>;
template class NMScriptableKernelFilter2Wrapper_Internal<double, double, 3>;


InstantiateObjectWrap( NMScriptableKernelFilter2Wrapper, NMScriptableKernelFilter2Wrapper_Internal )
SetNthInputWrap( NMScriptableKernelFilter2Wrapper, NMScriptableKernelFilter2Wrapper_Internal )
GetOutputWrap( NMScriptableKernelFilter2Wrapper, NMScriptableKernelFilter2Wrapper_Internal )
LinkInternalParametersWrap( NMScriptableKernelFilter2Wrapper, NMScriptableKernelFilter2Wrapper_Internal )
/*$<RATGetSupportWrap>$*/
SetRATWrap( NMScriptableKernelFilter2Wrapper, NMScriptableKernelFilter2Wrapper_Internal )


NMScriptableKernelFilter2Wrapper
::NMScriptableKernelFilter2Wrapper(QObject* parent)
{
    this->setParent(parent);
    this->setObjectName("NMScriptableKernelFilter2Wrapper");
    this->mParameterHandling = NMProcess::NM_USE_UP;

    mKernelShapeType = QString(tr("RECTANGULAR"));
    mKernelShapeEnum.clear();
    mKernelShapeEnum << "RECTANGULAR" << "CIRCULAR";
    mNumThreads = QThread::idealThreadCount() < 0 ? (unsigned int)1 : (unsigned int)QThread::idealThreadCount();
    this->mAuxDataIdx = 1;
    this->mInputNumBands = 1;
    this->mOutputNumBands = 1;
    this->mOutputNumDimensions = 2;
    this->mInputComponentType = otb::ImageIOBase::FLOAT;
    this->mOutputComponentType = otb::ImageIOBase::FLOAT;

    mUserProperties.clear();
    mUserProperties.insert(QStringLiteral("NMInputComponentType"), QStringLiteral("InputPixelType"));
    mUserProperties.insert(QStringLiteral("NMOutputComponentType"), QStringLiteral("OutputPixelType"));
    mUserProperties.insert(QStringLiteral("OutputNumDimensions"), QStringLiteral("NumDimensions"));
    mUserProperties.insert(QStringLiteral("Radius"), QStringLiteral("KernelRadius"));
    mUserProperties.insert(QStringLiteral("KernelShapeType"), QStringLiteral("KernelShape"));
    mUserProperties.insert(QStringLiteral("InitScript"), QStringLiteral("InitScript"));
    mUserProperties.insert(QStringLiteral("KernelScript"), QStringLiteral("KernelScript"));
    mUserProperties.insert(QStringLiteral("Nodata"), QStringLiteral("NodataValue"));
    mUserProperties.insert(QStringLiteral("NumThreads"), QStringLiteral("NumThreads"));

}

NMScriptableKernelFilter2Wrapper
::~NMScriptableKernelFilter2Wrapper()
{
}
