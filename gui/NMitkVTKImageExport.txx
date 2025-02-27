 /*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile: itkVTKImageExport.txx,v $
  Language:  C++
  Date:      $Date: 2007-10-05 10:29:52 $
  Version:   $Revision: 1.17 $

  Copyright (c) Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __NMitkVTKImageExport_txx
#define __NMitkVTKImageExport_txx

#include "NMitkVTKImageExport.h"

#include "itkPixelTraits.h"

namespace itk
{

/**
 * The constructor records the name of the pixel's scalar type for the
 * image to be sent to vtkImageImport's ScalarTypeCallback.
 */
template <class TInputImage>
NMVTKImageExport<TInputImage>::NMVTKImageExport()
{
  typedef typename TInputImage::PixelType         	        PixelType;
  typedef typename PixelTraits< PixelType >::ValueType      ScalarType;

  if(typeid(ScalarType) == typeid(double))
    {
    m_ScalarTypeName = "double";
    }
  else if(typeid(ScalarType) == typeid(float))
    {
    m_ScalarTypeName = "float";
    }
  else if(typeid(ScalarType) == typeid(long long))
    {
    m_ScalarTypeName = "long long";
    }
  else if(typeid(ScalarType) == typeid(unsigned long long))
    {
    m_ScalarTypeName = "unsigned long long";
    }
  else if(typeid(ScalarType) == typeid(long))
    {
    m_ScalarTypeName = "long";
    }
  else if(typeid(ScalarType) == typeid(unsigned long))
    {
    m_ScalarTypeName = "unsigned long";
    }
  else if(typeid(ScalarType) == typeid(int))
    {
    m_ScalarTypeName = "int";
    }
  else if(typeid(ScalarType) == typeid(unsigned int))
    {
    m_ScalarTypeName = "unsigned int";
    }
  else if(typeid(ScalarType) == typeid(short))
    {
    m_ScalarTypeName = "short";
    }
  else if(typeid(ScalarType) == typeid(unsigned short))
    {
    m_ScalarTypeName = "unsigned short";
    }
  else if(typeid(ScalarType) == typeid(char))
    {
    m_ScalarTypeName = "char";
    }
  else if(typeid(ScalarType) == typeid(unsigned char))
    {
    m_ScalarTypeName = "unsigned char";
    }
  else if(typeid(ScalarType) == typeid(signed char))
    {
    m_ScalarTypeName = "signed char";
    }
  else
    {
    itkExceptionMacro(<<"Type currently not supported");
    }
}

template <class TInputImage>
void NMVTKImageExport<TInputImage>::PrintSelf(std::ostream& os,
                                            Indent indent) const
{
  Superclass::PrintSelf(os, indent);
}


/**
 * Set the input image for this filter.
 */
template <class TInputImage>
void NMVTKImageExport<TInputImage>::SetInput(const InputImageType* input)
{
  this->ProcessObject::SetNthInput(0, 
                                   const_cast<TInputImage*>(input) );
}


/**
 * Get the current input image.
 */
template <class TInputImage>
typename NMVTKImageExport<TInputImage>::InputImageType *
NMVTKImageExport<TInputImage>::GetInput(void)
{
  return static_cast<TInputImage*>(
    this->ProcessObject::GetInput(0));
}


/**
 * Implements the WholeExtentCallback.  This returns a pointer to an
 * array of six integers describing the VTK-style extent of the image.
 * This corresponds to the ITK image's LargestPossibleRegion.
 */
template <class TInputImage>
int* NMVTKImageExport<TInputImage>::WholeExtentCallback()
{
  InputImagePointer input = this->GetInput();
  if( !input )
    {
    itkExceptionMacro(<< "Need to set an input");
    return 0;
    }

  InputRegionType region = input->GetLargestPossibleRegion();
  InputSizeType size = region.GetSize();
  InputIndexType index = region.GetIndex();

  unsigned int i=0;
  // Fill in the known portion of the extent.
  //std::cout << "ItkImgExp: WholeExtent: ";
  for(;i < InputImageDimension;++i)
    {
    m_WholeExtent[i*2] = int(index[i]);
    m_WholeExtent[i*2+1] = int(index[i]+size[i])-1;
    //std::cout << m_WholeExtent[i*2] << "-" << m_WholeExtent[i*2+1] << " ";
    }
    //  std::cout << std::endl;
  // Fill in the extent for dimensions up to three.
  for(;i < 3; ++i)
    {
    m_WholeExtent[i*2] = 0;
    m_WholeExtent[i*2+1] = 0;
    }
  return m_WholeExtent;
}


/**
 * Implements the SpacingCallback.  This returns a pointer to an array
 * of three floating point values describing the spacing of the image.
 */
template <class TInputImage>
double* NMVTKImageExport<TInputImage>::SpacingCallback()
{
  InputImagePointer input = this->GetInput();
  if( !input )
    {
    itkExceptionMacro(<< "Need to set an input");
    return 0;
    }

  const typename TInputImage::SpacingType& spacing = input->GetSpacing();
  const typename TInputImage::DirectionType& direction = input->GetDirection();

  unsigned int i=0;
  // Fill in the known portion of the spacing.
  for(;i < InputImageDimension;++i)
    {
    m_DataSpacing[i] = static_cast<double>(spacing[i] * direction[i][i]);
    }
  // Fill up the spacing with defaults up to three dimensions.
  for(;i < 3;++i)
    {
    m_DataSpacing[i] = 1;
    }
  return m_DataSpacing;
}

/**
 * Implements the SpacingCallback.  This returns a pointer to an array
 * of three floating point values describing the spacing of the image.
 */
template <class TInputImage>
float* NMVTKImageExport<TInputImage>::FloatSpacingCallback()
{
  InputImagePointer input = this->GetInput();
  const typename TInputImage::SpacingType& spacing = input->GetSpacing();
  const typename TInputImage::DirectionType& direction = input->GetDirection();

  unsigned int i=0;
  // Fill in the known portion of the spacing.
  for(;i < InputImageDimension;++i)
  {
      m_FloatDataSpacing[i] = static_cast<float>(spacing[i] * direction[i][i]);
  }
  // Fill up the spacing with defaults up to three dimensions.
  for(;i < 3;++i)
  {
      m_FloatDataSpacing[i] = 1;
  }
  return m_FloatDataSpacing;
}


/**
 * Implements the OriginCallback.  This returns a pointer to an array
 * of three floating point values describing the origin of the image.
 */
template <class TInputImage>
double* NMVTKImageExport<TInputImage>::OriginCallback()
{
  InputImagePointer input = this->GetInput();
  if( !input )
    {
    itkExceptionMacro(<< "Need to set an input");
    return 0;
    }

  const typename TInputImage::PointType& origin = input->GetOrigin();

  unsigned int i=0;
  for (; i < InputImageDimension; ++i)
  {
    m_DataOrigin[i] = origin[i];
  }

  // Fill up the origin with defaults up to three dimensions.
  for(;i < 3;++i)
  {
      m_DataOrigin[i] = 0;
  }
  return m_DataOrigin;
}

/**
 * Implements the OriginCallback.  This returns a pointer to an array
 * of three floating point values describing the origin of the image.
 */
template <class TInputImage>
float* NMVTKImageExport<TInputImage>::FloatOriginCallback()
{
  InputImagePointer input = this->GetInput();
  const typename TInputImage::PointType& origin = input->GetOrigin();

  unsigned int i=0;
  for (; i < InputImageDimension; ++i)
  {
      m_DataOrigin[i] = origin[i];
  }

  // Fill in the known portion of the origin.
  for(;i < InputImageDimension;++i)
  {
      m_FloatDataOrigin[i] = origin[i];
  }
  // Fill up the origin with defaults up to three dimensions.
  for(;i < 3;++i)
  {
      m_FloatDataOrigin[i] = 0;
  }
  return m_FloatDataOrigin;
}


/**
 * Implements the ScalarTypeCallback.  This returns the name of the
 * scalar value type of the image.
 */
template <class TInputImage>
const char* NMVTKImageExport<TInputImage>::ScalarTypeCallback()
{
  return m_ScalarTypeName.c_str();
}


/**
 * Implements the NumberOfComponentsCallback.  This returns the number of
 * components per pixel for the image.
 *
 * Currently, only one pixel component is supported by this class.
 */
template <class TInputImage>
int NMVTKImageExport<TInputImage>::NumberOfComponentsCallback()
{
  typedef typename TInputImage::PixelType                   PixelType;
  typedef typename PixelTraits< PixelType >::ValueType    ValueType;

  // on the assumption that there is no padding in this pixel type...
  //return sizeof( PixelType ) / sizeof( ValueType );
  return PixelTraits<PixelType>::Dimension;// * sizeof( ValueType );
}


/**
 * Implements the PropagateUpdateExtentCallback.  This takes the
 * update extent from VTK and translates it into a RequestedRegion for
 * the input image.  This requested region is then propagated through
 * the ITK pipeline.
 */
template <class TInputImage>
void NMVTKImageExport<TInputImage>::PropagateUpdateExtentCallback(int* extent)
{  
  InputSizeType size;
  InputIndexType index;

//  std::cout << "ItkImgExp: PropUpdateExtent: ";
  for(unsigned int i=0;i < InputImageDimension;++i)
    {
    index[i] = extent[i*2];
    size[i] = (extent[i*2+1]-extent[i*2])+1;
    }

  InputRegionType region;
  region.SetSize(size);
  region.SetIndex(index);
  
  InputImagePointer input = this->GetInput();
  if( !input )
    {
    itkExceptionMacro(<< "Need to set an input");
    return; 
    }

//  std::cout << "ulc: " << index[0] << ", " << index[1] << " size: ";
//  std::cout << size[0] << " x " << size[1] << std::endl;

  input->SetRequestedRegion(region);
}


/**
 * Implements the DataExtentCallback.  This returns a pointer to an
 * array of six integers describing the VTK-style extent of the
 * buffered portion of the image.  This corresponds to the ITK image's
 * BufferedRegion.
 */
template <class TInputImage>
int* NMVTKImageExport<TInputImage>::DataExtentCallback()
{
  InputImagePointer input = this->GetInput();
  if( !input )
    {
    itkExceptionMacro(<< "Need to set an input");
    return 0;
    }

  InputRegionType region = input->GetBufferedRegion();
  InputSizeType size = region.GetSize();
  InputIndexType index = region.GetIndex();

  unsigned int i=0;
  for(;i < InputImageDimension;++i)
    {
    m_DataExtent[i*2] = int(index[i]);
    m_DataExtent[i*2+1] = int(index[i]+size[i])-1;
    }
  for(;i < 3; ++i)
    {
    m_DataExtent[i*2] = 0;
    m_DataExtent[i*2+1] = 0;
    }
  return m_DataExtent;
}


/**
 * Implements the BufferPointerCallback.  This returns a pointer to
 * the image's memory buffer.
 */
template <class TInputImage>
void* NMVTKImageExport<TInputImage>::BufferPointerCallback()
{
  InputImagePointer input = this->GetInput();
  if( !input )
    {
    itkExceptionMacro(<< "Need to set an input");
    return 0;
    }

  return input->GetBufferPointer();
}

} // end namespace itk

#endif
