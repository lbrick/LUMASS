/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkImageImport.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*
    - adapted from the original vtkImageImport (VTK 8.2.0) for use in LUMASS
    - added support for long long data type
    by Alexander Herzig - August 2020

*/


#include "NMVtkImageImport.h"

#include "vtkByteSwap.h"
#include "vtkDataArray.h"
#include "vtkImageData.h"
#include "NMVtkImageImportExecutive.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkPointData.h"

#include <cctype>
#include <exception>

vtkStandardNewMacro(NMVtkImageImport);


#define tryCatchMacro(invocation, messagePrepend)\
    try\
    {\
      invocation;\
    }\
    catch (std::exception &_e)\
    {\
      vtkErrorMacro(<<messagePrepend <<_e.what());\
    }\
    catch (...)\
    {\
      vtkErrorMacro(<<"Unknown exception.");\
    }\


//----------------------------------------------------------------------------
NMVtkImageImport::NMVtkImageImport()
{
  int idx;

  this->ImportVoidPointer = nullptr;

  this->DataScalarType = VTK_SHORT;
  this->NumberOfScalarComponents = 1;

  for (idx = 0; idx < 3; ++idx)
  {
    this->DataExtent[idx*2] = this->DataExtent[idx*2 + 1] = 0;
    this->WholeExtent[idx*2] = this->WholeExtent[idx*2 + 1] = 0;
    this->DataSpacing[idx] = 1.0;
    this->DataOrigin[idx] = 0.0;
  }
  this->SaveUserArray = 0;

  this->CallbackUserData = nullptr;

  this->UpdateInformationCallback = nullptr;
  this->PipelineModifiedCallback = nullptr;
  this->WholeExtentCallback = nullptr;
  this->SpacingCallback = nullptr;
  this->OriginCallback = nullptr;
  this->ScalarTypeCallback = nullptr;
  this->NumberOfComponentsCallback = nullptr;
  this->PropagateUpdateExtentCallback = nullptr;
  this->UpdateDataCallback = nullptr;
  this->DataExtentCallback = nullptr;
  this->BufferPointerCallback = nullptr;

  this->SetNumberOfInputPorts(0);

  vtkExecutive *exec = NMVtkImageImportExecutive::New();
  this->SetExecutive(exec);
  exec->Delete();

  this->ScalarArrayName=nullptr;
  this->SetScalarArrayName("scalars");
}

//----------------------------------------------------------------------------
NMVtkImageImport::~NMVtkImageImport()
{
  if (!this->SaveUserArray)
  {
    delete [] static_cast<char *>(this->ImportVoidPointer);
  }
  this->SetScalarArrayName(nullptr);
}

//----------------------------------------------------------------------------
void NMVtkImageImport::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);

  int idx;

  os << indent << "ImportVoidPointer: " << this->ImportVoidPointer << "\n";

  os << indent << "DataScalarType: "
     << vtkImageScalarTypeNameMacro(this->DataScalarType) << "\n";

  os << indent << "NumberOfScalarComponents: "
     << this->NumberOfScalarComponents << "\n";

  os << indent << "WholeExtent: (" << this->WholeExtent[0];
  for (idx = 1; idx < 6; ++idx)
  {
    os << ", " << this->WholeExtent[idx];
  }
  os << ")\n";

  os << indent << "DataExtent: (" << this->DataExtent[0];
  for (idx = 1; idx < 6; ++idx)
  {
    os << ", " << this->DataExtent[idx];
  }
  os << ")\n";

  os << indent << "DataSpacing: (" << this->DataSpacing[0];
  for (idx = 1; idx < 3; ++idx)
  {
    os << ", " << this->DataSpacing[idx];
  }
  os << ")\n";

  os << indent << "DataOrigin: (" << this->DataOrigin[0];
  for (idx = 1; idx < 3; ++idx)
  {
    os << ", " << this->DataOrigin[idx];
  }
  os << ")\n";

  os << indent << "CallbackUserData: "
     << (this->CallbackUserData? "Set" : "Not Set") << "\n";

  os << indent << "UpdateInformationCallback: "
     << (this->UpdateInformationCallback? "Set" : "Not Set") << "\n";

  os << indent << "PipelineModifiedCallback: "
     << (this->PipelineModifiedCallback? "Set" : "Not Set") << "\n";

  os << indent << "WholeExtentCallback: "
     << (this->WholeExtentCallback? "Set" : "Not Set") << "\n";

  os << indent << "SpacingCallback: "
     << (this->SpacingCallback? "Set" : "Not Set") << "\n";

  os << indent << "OriginCallback: "
     << (this->OriginCallback? "Set" : "Not Set") << "\n";

  os << indent << "ScalarTypeCallback: "
     << (this->ScalarTypeCallback? "Set" : "Not Set") << "\n";

  os << indent << "NumberOfComponentsCallback: "
     << (this->NumberOfComponentsCallback? "Set" : "Not Set") << "\n";

  os << indent << "PropagateUpdateExtentCallback: "
     << (this->PropagateUpdateExtentCallback? "Set" : "Not Set") << "\n";

  os << indent << "UpdateDataCallback: "
     << (this->UpdateDataCallback? "Set" : "Not Set") << "\n";

  os << indent << "DataExtentCallback: "
     << (this->DataExtentCallback? "Set" : "Not Set") << "\n";

  os << indent << "BufferPointerCallback: "
     << (this->BufferPointerCallback? "Set" : "Not Set") << "\n";

  os << indent << "ScalarArrayName: ";
  if(this->ScalarArrayName!=nullptr)
  {
      os  << this->ScalarArrayName << endl;
  }
  else
  {
      os  << "(none)" << endl;
  }
}

//----------------------------------------------------------------------------
int NMVtkImageImport::RequestUpdateExtent(
  vtkInformation* vtkNotUsed(request),
  vtkInformationVector** vtkNotUsed(inputVector),
  vtkInformationVector* outputVector)
{
  if (this->PropagateUpdateExtentCallback)
  {
    int uExt[6];

    vtkInformation* outInfo = outputVector->GetInformationObject(0);
    outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(),uExt);
    tryCatchMacro(
      (this->PropagateUpdateExtentCallback)(this->CallbackUserData,uExt),
      "PropagateUpdateExtentCallback: ");
  }

  return 1;
}

//----------------------------------------------------------------------------
int NMVtkImageImport::ComputePipelineMTime(
  vtkInformation* request,
  vtkInformationVector** inInfoVec,
  vtkInformationVector* outInfoVec,
  int requestFromOutputPort,
  vtkMTimeType* mtime )
{
  if (this->InvokePipelineModifiedCallbacks())
  {
    this->Modified();
  }
  // Superclass normally returns our MTime.
  return Superclass::ComputePipelineMTime(request, inInfoVec, outInfoVec,
        requestFromOutputPort, mtime);
}

//----------------------------------------------------------------------------
int NMVtkImageImport::RequestInformation (
  vtkInformation * vtkNotUsed(request),
  vtkInformationVector ** vtkNotUsed( inputVector ),
  vtkInformationVector *outputVector)
{
  // get the info objects
  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  // If set, use the callbacks to fill in our data members.
  this->InvokeExecuteInformationCallbacks();

  // Legacy support for code that sets only DataExtent.
  this->LegacyCheckWholeExtent();

  // set the whole extent
  outInfo->Set(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(),
               this->WholeExtent,6);

  // set the spacing
  outInfo->Set(vtkDataObject::SPACING(),this->DataSpacing,3);

  // set the origin.
  outInfo->Set(vtkDataObject::ORIGIN(),this->DataOrigin,3);

  // set data type
  vtkDataObject::SetPointDataActiveScalarInfo(outInfo, this->DataScalarType,
    this->NumberOfScalarComponents);
  return 1;
}

//----------------------------------------------------------------------------
void NMVtkImageImport::ExecuteDataWithInformation(vtkDataObject *output,
                                                vtkInformation* outInfo)
{
  // If set, use the callbacks to prepare our input data.
  this->InvokeExecuteDataCallbacks();

  vtkImageData *data = vtkImageData::SafeDownCast(output);
  data->SetExtent(0,0,0,0,0,0);
  data->AllocateScalars(outInfo);
  void *ptr = this->GetImportVoidPointer();
  vtkIdType size = this->NumberOfScalarComponents;
  size *= this->DataExtent[1] - this->DataExtent[0] + 1;
  size *= this->DataExtent[3] - this->DataExtent[2] + 1;
  size *= this->DataExtent[5] - this->DataExtent[4] + 1;

  data->SetExtent(this->DataExtent);
  data->GetPointData()->GetScalars()->SetVoidArray(ptr,size,1);
  data->GetPointData()->GetScalars()->SetName(this->ScalarArrayName);
}

//----------------------------------------------------------------------------
void NMVtkImageImport::CopyImportVoidPointer(void *ptr, vtkIdType size)
{
  unsigned char *mem = new unsigned char [size];
  memcpy(mem,ptr,size);
  this->SetImportVoidPointer(mem,0);
}

//----------------------------------------------------------------------------
void NMVtkImageImport::SetImportVoidPointer(void *ptr)
{
  this->SetImportVoidPointer(ptr,1);
}

//----------------------------------------------------------------------------
void NMVtkImageImport::SetImportVoidPointer(void *ptr, int save)
{
  if (ptr != this->ImportVoidPointer)
  {
    if ((this->ImportVoidPointer) && (!this->SaveUserArray))
    {
      vtkDebugMacro (<< "Deleting the array...");
      delete [] static_cast<char *>(this->ImportVoidPointer);
    }
    else
    {
      vtkDebugMacro (<<"Warning, array not deleted, but will point to new array.");
    }
    this->Modified();
  }
  this->SaveUserArray = save;
  this->ImportVoidPointer = ptr;
}

//----------------------------------------------------------------------------
int NMVtkImageImport::InvokePipelineModifiedCallbacks()
{
  if (this->PipelineModifiedCallback)
  {
    int ret;
    try
    {
      ret = (this->PipelineModifiedCallback)(this->CallbackUserData);
    }
    catch (std::exception &_e)
    {
      vtkErrorMacro(<<"Calling PipelineModifiedCallback: " << _e.what());
      // if an error occurred, we don't want the pipeline to run again
      // until the error has been rectified.  It can be assumed that
      // the rectifying actions will set the modified flag.
      ret = 0;
    }
    catch (...)
    {
      vtkErrorMacro(<<"Unknown exception.");
      // same logic as above
      ret = 0;
    }

    return ret;
  }
  else
  {
    // if there's no PipelineModified installed, we return 0
    return 0;
  }

}

//----------------------------------------------------------------------------
void NMVtkImageImport::InvokeUpdateInformationCallbacks()
{
  if (this->UpdateInformationCallback)
  {
    tryCatchMacro((this->UpdateInformationCallback)(this->CallbackUserData),
                  "Calling UpdateInformationCallback: ");
  }

  if (this->InvokePipelineModifiedCallbacks())
  {
    this->Modified();
  }
}

//----------------------------------------------------------------------------
void NMVtkImageImport::InvokeExecuteInformationCallbacks()
{
  if (this->WholeExtentCallback)
  {
    tryCatchMacro(
      this->SetWholeExtent(
        (this->WholeExtentCallback)(this->CallbackUserData)),
      "Calling WholeExtentCallback: ");
  }
  if (this->SpacingCallback)
  {
    tryCatchMacro(
      this->SetDataSpacing((this->SpacingCallback)(this->CallbackUserData)),
      "Calling SpacingCallback: ");
  }
  if (this->OriginCallback)
  {
    tryCatchMacro(
      this->SetDataOrigin((this->OriginCallback)(this->CallbackUserData)),
      "Calling OriginCallback: ");
  }
  if (this->NumberOfComponentsCallback)
  {
    tryCatchMacro(
      this->SetNumberOfScalarComponents(
        (this->NumberOfComponentsCallback)(this->CallbackUserData)),
      "Calling NumberOfComponentsCallback: ");
  }
  if (this->ScalarTypeCallback)
  {
    const char* scalarType = "double"; // default
    tryCatchMacro(
      scalarType = (this->ScalarTypeCallback)(this->CallbackUserData),
      "Calling ScalarTypeCallback: ");

    if (strcmp(scalarType, "double")==0)
    {
      this->SetDataScalarType(VTK_DOUBLE);
    }
    else if (strcmp(scalarType, "float")==0)
    {
      this->SetDataScalarType(VTK_FLOAT);
    }
    else if (strcmp(scalarType, "long")==0)
    {
      this->SetDataScalarType(VTK_LONG);
    }
    else if (strcmp(scalarType, "unsigned long")==0)
    {
      this->SetDataScalarType(VTK_UNSIGNED_LONG);
    }
    else if (strcmp(scalarType, "long long") == 0)
    {
        this->SetDataScalarType(VTK_LONG_LONG);
    }
    else if (strcmp(scalarType, "unsigned long long") == 0)
    {
        this->SetDataScalarType(VTK_UNSIGNED_LONG_LONG);
    }
    else if (strcmp(scalarType, "int")==0)
    {
      this->SetDataScalarType(VTK_INT);
    }
    else if (strcmp(scalarType, "unsigned int")==0)
    {
      this->SetDataScalarType(VTK_UNSIGNED_INT);
    }
    else if (strcmp(scalarType, "short")==0)
    {
      this->SetDataScalarType(VTK_SHORT);
    }
    else if (strcmp(scalarType, "unsigned short")==0)
    {
      this->SetDataScalarType(VTK_UNSIGNED_SHORT);
    }
    else if (strcmp(scalarType, "char")==0)
    {
      this->SetDataScalarType(VTK_CHAR);
    }
    else if (strcmp(scalarType, "unsigned char")==0)
    {
      this->SetDataScalarType(VTK_UNSIGNED_CHAR);
    }
    else if (strcmp(scalarType, "signed char")==0)
    {
      this->SetDataScalarType(VTK_SIGNED_CHAR);
    }
  }
}


//----------------------------------------------------------------------------
void NMVtkImageImport::InvokeExecuteDataCallbacks()
{
  if (this->UpdateDataCallback)
  {
    tryCatchMacro(
      (this->UpdateDataCallback)(this->CallbackUserData),
      "Calling UpdateDataCallback: ");
  }
  if (this->DataExtentCallback)
  {
    tryCatchMacro(
      this->SetDataExtent((this->DataExtentCallback)(this->CallbackUserData)),
      "Calling DataExtentCallback: ");
  }
  if (this->BufferPointerCallback)
  {
    tryCatchMacro(
      this->SetImportVoidPointer(
        (this->BufferPointerCallback)(this->CallbackUserData)),
      "Calling BufferPointerCallback: ");
  }
}

//----------------------------------------------------------------------------
// In the past, this class made no distinction between whole extent and
// buffered extent, so only SetDataExtent also set the whole extent of
// the output.  Now, there is a separate SetWholeExtent which should be
// called as well.
void NMVtkImageImport::LegacyCheckWholeExtent()
{
  // If the WholeExtentCallback is set, this must not be legacy code.
  if (this->WholeExtentCallback)
  {
    return;
  }
  int i;
  // Check whether the whole extent has been set.
  for(i=0; i < 6; ++i)
  {
    if (this->WholeExtent[i] != 0)
    {
      return;
    }
  }

  // The whole extent has not been set.  Copy it from the data extent
  // and issue a warning.
  for(i=0; i < 6; ++i)
  {
    this->WholeExtent[i] = this->DataExtent[i];
  }

  vtkWarningMacro("\n"
    "There is a distinction between the whole extent and the buffered\n"
    "extent of an imported image.  Use SetWholeExtent to set the extent\n"
    "of the entire image.  Use SetDataExtent to set the extent of the\n"
    "portion of the image that is in the buffer set with\n"
    "SetImportVoidPointer.  Both should be called even if the extents are\n"
    "the same.");
}
