/*=========================================================================

  Program:   Visualization Toolkit

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

/*!
 *  adapted from vtkOpenGLPolyDataMapper by Alex Herzig for use in LUMASS, December 2018
 *  (c) 2018, Landcare Research New Zealand Ltd.
 *
*/

#include "NMVtkOpenGLPolyDataMapper.h"

#include "vtkCamera.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkCommand.h"
#include "vtkFloatArray.h"
#include "vtkLongArray.h"
#include "vtkHardwareSelector.h"
#include "vtkImageData.h"
#include "vtkInformation.h"
#include "vtkLight.h"
#include "vtkLightCollection.h"
#include "vtkLightingMapPass.h"
#include "vtkMath.h"
#include "vtkMatrix3x3.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkOpenGLActor.h"
#include "vtkOpenGLBufferObject.h"
#include "vtkOpenGLCamera.h"
#include "vtkOpenGLError.h"
#include "vtkOpenGLHelper.h"
#include "vtkOpenGLIndexBufferObject.h"
#include "vtkOpenGLRenderPass.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGLRenderer.h"
#include "vtkOpenGLRenderTimer.h"
#include "vtkOpenGLResourceFreeCallback.h"
#include "vtkOpenGLShaderCache.h"
#include "vtkOpenGLState.h"
#include "vtkOpenGLTexture.h"
#include "vtkOpenGLVertexArrayObject.h"
#include "vtkOpenGLVertexBufferObject.h"
#include "vtkOpenGLVertexBufferObjectCache.h"
#include "vtkOpenGLVertexBufferObjectGroup.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkProperty.h"
#include "vtkScalarsToColors.h"
#include "vtkShaderProgram.h"
#include "vtkTextureObject.h"
#include "vtkTransform.h"
#include "vtkUnsignedIntArray.h"
#include "vtkScalarsToColors.h"
#include "vtkLookupTable.h"
#include "vtkDoubleArray.h"
#include "vtkIdTypeArray.h"

#include "NMPolygonToTriangles.h"
#include "vtkLookupTable.h"

// Bring in our fragment lit shader symbols.
#include "vtkPolyDataVS.h"
#include "vtkPolyDataFS.h"
#include "vtkPolyDataWideLineGS.h"

#include <algorithm>
#include <sstream>


//-----------------------------------------------------------------------------
vtkStandardNewMacro(NMVtkOpenGLPolyDataMapper)

//-----------------------------------------------------------------------------
NMVtkOpenGLPolyDataMapper::NMVtkOpenGLPolyDataMapper()
  : UsingScalarColoring(false),
    TimerQuery(new vtkOpenGLRenderTimer)
{
  this->InternalColorTexture = nullptr;
  this->PopulateSelectionSettings = 1;
  this->LastSelectionState = vtkHardwareSelector::MIN_KNOWN_PASS - 1;
  this->CurrentInput = nullptr;
  this->TempMatrix4 = vtkMatrix4x4::New();
  this->TempMatrix3 = vtkMatrix3x3::New();
  this->DrawingEdgesOrVertices = false;
  this->ForceTextureCoordinates = false;

  this->LastColorChange = 0;

  this->PrimitiveIDOffset = 0;
  this->ShiftScaleMethod =
    vtkOpenGLVertexBufferObject::AUTO_SHIFT_SCALE;

  this->CellScalarTexture = nullptr;
  this->CellScalarBuffer = nullptr;
  this->CellNormalTexture = nullptr;
  this->CellNormalBuffer = nullptr;

  this->HaveCellScalars = false;
  this->HaveCellNormals = false;

  this->PointIdArrayName = nullptr;
  this->CellIdArrayName = nullptr;
  this->ProcessIdArrayName = nullptr;
  this->CompositeIdArrayName = nullptr;
  this->VBOs = vtkOpenGLVertexBufferObjectGroup::New();

  this->AppleBugPrimIDBuffer = nullptr;
  this->HaveAppleBug = false;
  this->HaveAppleBugForce = 0;
  this->LastBoundBO = nullptr;

  this->VertexShaderCode = nullptr;
  this->FragmentShaderCode = nullptr;
  this->GeometryShaderCode = nullptr;
//  this->TessControlShaderCode = nullptr;
//  this->TessEvaluationShaderCode = nullptr;

  for (int i = PrimitiveStart; i < PrimitiveEnd; i++)
  {
    this->LastLightComplexity[&this->Primitives[i]] = -1;
    this->LastLightCount[&this->Primitives[i]] = 0;
    this->Primitives[i].PrimitiveType = i;
  }

  this->ResourceCallback = new vtkOpenGLResourceFreeCallback<NMVtkOpenGLPolyDataMapper>(this,
    &NMVtkOpenGLPolyDataMapper::ReleaseGraphicsResources);

  // initialize to 1 as 0 indicates we have initiated a request
  this->TimerQueryCounter = 1;
  this->TimeToDraw = 0.0001;
}

//-----------------------------------------------------------------------------
NMVtkOpenGLPolyDataMapper::~NMVtkOpenGLPolyDataMapper()
{
  if (this->ResourceCallback)
  {
    this->ResourceCallback->Release();
    delete this->ResourceCallback;
    this->ResourceCallback = nullptr;
  }
  if (this->InternalColorTexture)
  { // Resources released previously.
    this->InternalColorTexture->Delete();
    this->InternalColorTexture = nullptr;
  }
  this->TempMatrix3->Delete();
  this->TempMatrix4->Delete();

  if (this->CellScalarTexture)
  { // Resources released previously.
    this->CellScalarTexture->Delete();
    this->CellScalarTexture = nullptr;
  }
  if (this->CellScalarBuffer)
  { // Resources released previously.
    this->CellScalarBuffer->Delete();
    this->CellScalarBuffer = nullptr;
  }

  if (this->CellNormalTexture)
  { // Resources released previously.
    this->CellNormalTexture->Delete();
    this->CellNormalTexture = nullptr;
  }
  if (this->CellNormalBuffer)
  { // Resources released previously.
    this->CellNormalBuffer->Delete();
    this->CellNormalBuffer = nullptr;
  }

  this->SetPointIdArrayName(nullptr);
  this->SetCellIdArrayName(nullptr);
  this->SetProcessIdArrayName(nullptr);
  this->SetCompositeIdArrayName(nullptr);
  this->VBOs->Delete();
  this->VBOs = nullptr;

  if (this->AppleBugPrimIDBuffer)
  {
    this->AppleBugPrimIDBuffer->Delete();
  }

  this->SetVertexShaderCode(nullptr);
  this->SetFragmentShaderCode(nullptr);
  this->SetGeometryShaderCode(nullptr);
  delete TimerQuery;
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::ReleaseGraphicsResources(vtkWindow* win)
{
  if (!this->ResourceCallback->IsReleasing())
  {
    this->ResourceCallback->Release();
    return;
  }

  this->VBOs->ReleaseGraphicsResources(win);
  for (int i = PrimitiveStart; i < PrimitiveEnd; i++)
  {
    this->Primitives[i].ReleaseGraphicsResources(win);
  }

  if (this->InternalColorTexture)
  {
    this->InternalColorTexture->ReleaseGraphicsResources(win);
  }
  if (this->CellScalarTexture)
  {
    this->CellScalarTexture->ReleaseGraphicsResources(win);
  }
  if (this->CellScalarBuffer)
  {
    this->CellScalarBuffer->ReleaseGraphicsResources();
  }
  if (this->CellNormalTexture)
  {
    this->CellNormalTexture->ReleaseGraphicsResources(win);
  }
  if (this->CellNormalBuffer)
  {
    this->CellNormalBuffer->ReleaseGraphicsResources();
  }
  if (this->AppleBugPrimIDBuffer)
  {
    this->AppleBugPrimIDBuffer->ReleaseGraphicsResources();
  }
  this->TimerQuery->ReleaseGraphicsResources();
  this->VBOBuildState.Clear();
  this->IBOBuildState.Clear();
  this->CellTextureBuildState.Clear();
  this->Modified();
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::AddShaderReplacement(
    vtkShader::Type shaderType, // vertex, fragment, etc
    const std::string& originalValue,
    bool replaceFirst,  // do this replacement before the default
    const std::string& replacementValue,
    bool replaceAll)
{
  vtkShader::ReplacementSpec spec;
  spec.ShaderType = shaderType;
  spec.OriginalValue = originalValue;
  spec.ReplaceFirst = replaceFirst;

  vtkShader::ReplacementValue values;
  values.Replacement = replacementValue;
  values.ReplaceAll = replaceAll;

  this->UserShaderReplacements[spec] = values;
  this->Modified();
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::ClearShaderReplacement(
    vtkShader::Type shaderType, // vertex, fragment, etc
    const std::string& originalValue,
    bool replaceFirst)
{
  vtkShader::ReplacementSpec spec;
  spec.ShaderType = shaderType;
  spec.OriginalValue = originalValue;
  spec.ReplaceFirst = replaceFirst;

  typedef std::map<const vtkShader::ReplacementSpec,
    vtkShader::ReplacementValue>::iterator RIter;
  RIter found = this->UserShaderReplacements.find(spec);
  if (found != this->UserShaderReplacements.end())
  {
    this->UserShaderReplacements.erase(found);
    this->Modified();
  }
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::ClearAllShaderReplacements(
  vtkShader::Type shaderType)
{
  // First clear all shader code
  if ((shaderType == vtkShader::Vertex) && this->VertexShaderCode)
  {
    this->SetVertexShaderCode(nullptr);
  }
  else if ((shaderType == vtkShader::Fragment) && this->FragmentShaderCode)
  {
    this->SetFragmentShaderCode(nullptr);
  }
  else if ((shaderType == vtkShader::Geometry) && this->GeometryShaderCode)
  {
    this->SetGeometryShaderCode(nullptr);
  }
//  else if ((shaderType == vtkShader::TessControl) &&  this->TessControlShaderCode)
//  {
//      this->SetTessControlShaderCode(nullptr);
//  }
//  else if ((shaderType == vtkShader::TessEvaluation) && this->TessEvaluationShaderCode)
//  {
//      this->SetTessControlShaderCode(nullptr);
//  }

  // Now clear custom tag replacements
  std::map<const vtkShader::ReplacementSpec,
           vtkShader::ReplacementValue>::iterator rIter;
  for (rIter = this->UserShaderReplacements.begin();
       rIter != this->UserShaderReplacements.end();)
  {
    if (rIter->first.ShaderType == shaderType)
    {
      this->UserShaderReplacements.erase(rIter++);
      this->Modified();
    }
    else
    {
      ++rIter;
    }
  }
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::ClearAllShaderReplacements()
{
  this->SetVertexShaderCode(nullptr);
  this->SetFragmentShaderCode(nullptr);
  this->SetGeometryShaderCode(nullptr);
//  this->SetTessControlShaderCode(nullptr);
//  this->SetTessEvaluationShaderCode(nullptr);
  this->UserShaderReplacements.clear();
  this->Modified();
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::BuildShaders(
    std::map<vtkShader::Type, vtkShader *> shaders,
    vtkRenderer *ren, vtkActor *actor)
{
  this->GetShaderTemplate(shaders, ren, actor);

  typedef std::map<const vtkShader::ReplacementSpec,
    vtkShader::ReplacementValue>::const_iterator RIter;

  // user specified pre replacements
  for (RIter i = this->UserShaderReplacements.begin();
    i != this->UserShaderReplacements.end(); ++i)
  {
    if (i->first.ReplaceFirst)
    {
      std::string ssrc = shaders[i->first.ShaderType]->GetSource();
      vtkShaderProgram::Substitute(ssrc,
        i->first.OriginalValue,
        i->second.Replacement,
        i->second.ReplaceAll);
      shaders[i->first.ShaderType]->SetSource(ssrc);
    }
  }

  this->ReplaceShaderValues(shaders, ren, actor);

  // user specified post replacements
  for (RIter i = this->UserShaderReplacements.begin();
    i != this->UserShaderReplacements.end(); ++i)
  {
    if (!i->first.ReplaceFirst)
    {
      std::string ssrc = shaders[i->first.ShaderType]->GetSource();
      vtkShaderProgram::Substitute(ssrc,
        i->first.OriginalValue,
        i->second.Replacement,
        i->second.ReplaceAll);
      shaders[i->first.ShaderType]->SetSource(ssrc);
    }
  }
}

//-----------------------------------------------------------------------------
bool NMVtkOpenGLPolyDataMapper::HaveWideLines(
  vtkRenderer *ren,
  vtkActor *actor)
{
  if (this->GetOpenGLMode(actor->GetProperty()->GetRepresentation(),
      this->LastBoundBO->PrimitiveType) == GL_LINES
      && actor->GetProperty()->GetLineWidth() > 1.0)
  {
    // we have wide lines, but the OpenGL implementation may
    // actually support them, check the range to see if we
      // really need have to implement our own wide lines
    vtkOpenGLRenderWindow *renWin = vtkOpenGLRenderWindow::SafeDownCast(ren->GetRenderWindow());
    return actor->GetProperty()->GetRenderLinesAsTubes() || !(renWin &&
      renWin->GetMaximumHardwareLineWidth() >= actor->GetProperty()->GetLineWidth());
  }
  return false;
}

//-----------------------------------------------------------------------------
vtkMTimeType NMVtkOpenGLPolyDataMapper::GetRenderPassStageMTime(vtkActor *actor)
{
  vtkInformation *info = actor->GetPropertyKeys();
  vtkMTimeType renderPassMTime = 0;

  int curRenderPasses = 0;
  if (info && info->Has(vtkOpenGLRenderPass::RenderPasses()))
  {
    curRenderPasses = info->Length(vtkOpenGLRenderPass::RenderPasses());
  }

  int lastRenderPasses = 0;
  if (this->LastRenderPassInfo->Has(vtkOpenGLRenderPass::RenderPasses()))
  {
    lastRenderPasses =
        this->LastRenderPassInfo->Length(vtkOpenGLRenderPass::RenderPasses());
  }
  else // have no last pass
  {
    if (!info) // have no current pass
    {
      return 0; // short circuit
    }
  }

  // Determine the last time a render pass changed stages:
  if (curRenderPasses != lastRenderPasses)
  {
    // Number of passes changed, definitely need to update.
    // Fake the time to force an update:
    renderPassMTime = VTK_MTIME_MAX;
  }
  else
  {
    // Compare the current to the previous render passes:
    for (int i = 0; i < curRenderPasses; ++i)
    {
      vtkObjectBase *curRP = info->Get(vtkOpenGLRenderPass::RenderPasses(), i);
      vtkObjectBase *lastRP =
          this->LastRenderPassInfo->Get(vtkOpenGLRenderPass::RenderPasses(), i);

      if (curRP != lastRP)
      {
        // Render passes have changed. Force update:
        renderPassMTime = VTK_MTIME_MAX;
        break;
      }
      else
      {
        // Render passes have not changed -- check MTime.
        vtkOpenGLRenderPass *rp = static_cast<vtkOpenGLRenderPass*>(curRP);
        renderPassMTime = std::max(renderPassMTime, rp->GetShaderStageMTime());
      }
    }
  }

  // Cache the current set of render passes for next time:
  if (info)
  {
    this->LastRenderPassInfo->CopyEntry(info,
                                        vtkOpenGLRenderPass::RenderPasses());
  }
  else
  {
    this->LastRenderPassInfo->Clear();
  }

  return renderPassMTime;
}

std::string NMVtkOpenGLPolyDataMapper::GetTextureCoordinateName(const char *tname)
{
  for (auto it : this->ExtraAttributes)
  {
    if (it.second.TextureName == tname)
    {
      return it.first;
    }
  }
  return std::string("tcoord");
}

//-----------------------------------------------------------------------------
bool NMVtkOpenGLPolyDataMapper::HaveTextures(vtkActor *actor)
{
  return (this->GetNumberOfTextures(actor) > 0);
}

typedef std::pair<vtkTexture *, std::string> texinfo;

//-----------------------------------------------------------------------------
unsigned int NMVtkOpenGLPolyDataMapper::GetNumberOfTextures(vtkActor *actor)
{
  unsigned int res = 0;
  if (this->ColorTextureMap)
  {
    res++;
  }
  if (actor->GetTexture())
  {
    res++;
  }
  res += actor->GetProperty()->GetNumberOfTextures();
  return res;
}

//-----------------------------------------------------------------------------
std::vector<texinfo> NMVtkOpenGLPolyDataMapper::GetTextures(vtkActor *actor)
{
  std::vector<texinfo> res;

  if (this->ColorTextureMap)
  {
    res.push_back(texinfo(this->InternalColorTexture, "colortexture"));
  }
  if (actor->GetTexture())
  {
    res.push_back(texinfo(actor->GetTexture(), "actortexture"));
  }
  auto textures = actor->GetProperty()->GetAllTextures();
  for (auto ti : textures)
  {
    res.push_back(texinfo(ti.second,ti.first));
  }
  return res;
}

//-----------------------------------------------------------------------------
bool NMVtkOpenGLPolyDataMapper::HaveTCoords(vtkPolyData *poly)
{
  return (this->ColorCoordinates ||
          poly->GetPointData()->GetTCoords() ||
          this->ForceTextureCoordinates);
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::GetShaderTemplate(
    std::map<vtkShader::Type, vtkShader *> shaders,
    vtkRenderer *ren, vtkActor *actor)
{
  if (this->VertexShaderCode && strcmp(this->VertexShaderCode,"") != 0)
  {
    shaders[vtkShader::Vertex]->SetSource(this->VertexShaderCode);
  }
  else
  {
    shaders[vtkShader::Vertex]->SetSource(vtkPolyDataVS);
  }

  if (this->FragmentShaderCode && strcmp(this->FragmentShaderCode,"") != 0)
  {
    shaders[vtkShader::Fragment]->SetSource(this->FragmentShaderCode);
  }
  else
  {
    shaders[vtkShader::Fragment]->SetSource(vtkPolyDataFS);
  }

//  if (this->TessControlShaderCode && strcmp(this->TessControlShaderCode,"") != 0)
//  {
//      shaders[vtkShader::TessControl]->SetSource(this->TessControlShaderCode);
//  }

//  if (this->TessEvaluationShaderCode && strcmp(this->TessEvaluationShaderCode, "") != 0)
//  {
//      shaders[vtkShader::TessEvaluation]->SetSource(this->TessEvaluationShaderCode);
//  }

  if (this->GeometryShaderCode && strcmp(this->GeometryShaderCode,"") != 0)
  {
    shaders[vtkShader::Geometry]->SetSource(this->GeometryShaderCode);
  }
  else
  {
    if (this->HaveWideLines(ren, actor))
    {
      shaders[vtkShader::Geometry]->SetSource(vtkPolyDataWideLineGS);
    }
    else
    {
      shaders[vtkShader::Geometry]->SetSource("");
    }
  }
}

//------------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::ReplaceShaderRenderPass(
    std::map<vtkShader::Type, vtkShader *> shaders, vtkRenderer *,
    vtkActor *act, bool prePass)
{
  std::string VSSource = shaders[vtkShader::Vertex]->GetSource();
  std::string GSSource = shaders[vtkShader::Geometry]->GetSource();
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();
//  std::string CSSource = shaders[vtkShader::TessControl]->GetSource();
//  std::string ESSource = shaders[vtkShader::TessEvaluation]->GetSource();

  vtkInformation *info = act->GetPropertyKeys();
  if (info && info->Has(vtkOpenGLRenderPass::RenderPasses()))
  {
    int numRenderPasses = info->Length(vtkOpenGLRenderPass::RenderPasses());
    for (int i = 0; i < numRenderPasses; ++i)
    {
      vtkObjectBase *rpBase = info->Get(vtkOpenGLRenderPass::RenderPasses(), i);
      vtkOpenGLRenderPass *rp = static_cast<vtkOpenGLRenderPass*>(rpBase);
      if (prePass)
      {
        if (!rp->PreReplaceShaderValues(VSSource, GSSource, FSSource,
           this, act))
        {
          vtkErrorMacro("vtkOpenGLRenderPass::ReplaceShaderValues failed for "
                        << rp->GetClassName());
        }
      }
      else
      {
        if (!rp->PostReplaceShaderValues(VSSource, GSSource, FSSource,
           this, act))
        {
          vtkErrorMacro("vtkOpenGLRenderPass::ReplaceShaderValues failed for "
                        << rp->GetClassName());
        }
      }
    }
  }

  shaders[vtkShader::Vertex]->SetSource(VSSource);
  shaders[vtkShader::Geometry]->SetSource(GSSource);
  shaders[vtkShader::Fragment]->SetSource(FSSource);
//  shaders[vtkShader::TessControl]->SetSource(CSSource);
//  shaders[vtkShader::TessEvaluation]->SetSource(ESSource);
}

//------------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::ReplaceShaderColor(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *ren, vtkActor *actor)
{
  std::string VSSource = shaders[vtkShader::Vertex]->GetSource();
  std::string GSSource = shaders[vtkShader::Geometry]->GetSource();
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();
//  std::string CSSource = shaders[vtkShader::TessControl]->GetSource();
//  std::string ESSource = shaders[vtkShader::TessEvaluation]->GetSource();

  // these are always defined
  std::string colorDec =
    "uniform float ambientIntensity; // the material ambient\n"
    "uniform float diffuseIntensity; // the material diffuse\n"
    "uniform float opacityUniform; // the fragment opacity\n"
    "uniform vec3 ambientColorUniform; // ambient color\n"
    "uniform vec3 diffuseColorUniform; // diffuse color\n";

  std::string colorImpl;

  // specular lighting?
  if (this->LastLightComplexity[this->LastBoundBO])
  {
    colorDec +=
      "uniform float specularIntensity; // the material specular intensity\n"
      "uniform vec3 specularColorUniform; // intensity weighted color\n"
      "uniform float specularPowerUniform;\n";
    colorImpl +=
      "  vec3 specularColor = specularIntensity * specularColorUniform;\n"
      "  float specularPower = specularPowerUniform;\n";
  }

  // for point picking we render primitives as points
  // that means cell scalars will not have correct
  // primitiveIds to lookup into the texture map
  // so we must skip cell scalar coloring when point picking
  // The boolean will be used in an else clause below
  vtkHardwareSelector* selector = ren->GetSelector();
  bool pointPicking = false;
  if (selector &&
      selector->GetFieldAssociation() == vtkDataObject::FIELD_ASSOCIATION_POINTS)
  {
    pointPicking = true;
  }

  // handle color point attributes
  if (this->VBOs->GetNumberOfComponents("scalarColor") != 0 &&
      !this->DrawingEdgesOrVertices)
  {
    vtkShaderProgram::Substitute(VSSource,"//VTK::Color::Dec",
                        "in vec4 scalarColor;\n"
                        "out vec4 vertexColorVSOutput;");
    vtkShaderProgram::Substitute(VSSource,"//VTK::Color::Impl",
                        "vertexColorVSOutput = scalarColor;");
    vtkShaderProgram::Substitute(GSSource,
      "//VTK::Color::Dec",
      "in vec4 vertexColorVSOutput[];\n"
      "out vec4 vertexColorGSOutput;");
    vtkShaderProgram::Substitute(GSSource,
      "//VTK::Color::Impl",
      "vertexColorGSOutput = vertexColorVSOutput[i];");

    colorDec += "in vec4 vertexColorVSOutput;\n";
    colorImpl +=
      "  vec3 ambientColor = ambientIntensity * vertexColorVSOutput.rgb;\n"
      "  vec3 diffuseColor = diffuseIntensity * vertexColorVSOutput.rgb;\n"
      "  float opacity = opacityUniform * vertexColorVSOutput.a;";
  }
  // handle point color texture map coloring
  else if (this->InterpolateScalarsBeforeMapping &&
      this->ColorCoordinates &&
      !this->DrawingEdgesOrVertices)
  {
    colorImpl +=
      "  vec4 texColor = texture(colortexture, tcoordVCVSOutput.st);\n"
      "  vec3 ambientColor = ambientIntensity * texColor.rgb;\n"
      "  vec3 diffuseColor = diffuseIntensity * texColor.rgb;\n"
      "  float opacity = opacityUniform * texColor.a;";
  }
  // are we doing cell scalar coloring by texture?
  else if (this->HaveCellScalars && !this->DrawingEdgesOrVertices && !pointPicking)
  {
    colorImpl +=
      "  vec4 texColor = texelFetchBuffer(textureC, gl_PrimitiveID + PrimitiveIDOffset);\n"
      "  vec3 ambientColor = ambientIntensity * texColor.rgb;\n"
      "  vec3 diffuseColor = diffuseIntensity * texColor.rgb;\n"
      "  float opacity = opacityUniform * texColor.a;";
  }
  // just material but handle backfaceproperties
  else
  {
    colorImpl +=
      "  vec3 ambientColor = ambientIntensity * ambientColorUniform;\n"
      "  vec3 diffuseColor = diffuseIntensity * diffuseColorUniform;\n"
      "  float opacity = opacityUniform;\n";

    if (actor->GetBackfaceProperty() && !this->DrawingEdgesOrVertices)
    {
      colorDec +=
        "uniform float opacityUniformBF; // the fragment opacity\n"
        "uniform float ambientIntensityBF; // the material ambient\n"
        "uniform float diffuseIntensityBF; // the material diffuse\n"
        "uniform vec3 ambientColorUniformBF; // ambient material color\n"
        "uniform vec3 diffuseColorUniformBF; // diffuse material color\n";
      if (this->LastLightComplexity[this->LastBoundBO])
      {
        colorDec +=
          "uniform float specularIntensityBF; // the material specular intensity\n"
          "uniform vec3 specularColorUniformBF; // intensity weighted color\n"
          "uniform float specularPowerUniformBF;\n";
        colorImpl +=
          "  if (gl_FrontFacing == false) {\n"
          "    ambientColor = ambientIntensityBF * ambientColorUniformBF;\n"
          "    diffuseColor = diffuseIntensityBF * diffuseColorUniformBF;\n"
          "    specularColor = specularIntensityBF * specularColorUniformBF;\n"
          "    specularPower = specularPowerUniformBF;\n"
          "    opacity = opacityUniformBF; }\n";
      }
      else
      {
        colorImpl +=
          "  if (gl_FrontFacing == false) {\n"
          "    ambientColor = ambientIntensityBF * ambientColorUniformBF;\n"
          "    diffuseColor = diffuseIntensityBF * diffuseColorUniformBF;\n"
          "    opacity = opacityUniformBF; }\n";
      }
    }
  }

  if (this->HaveCellScalars && !this->DrawingEdgesOrVertices)
  {
    colorDec += "uniform samplerBuffer textureC;\n";
  }

  vtkShaderProgram::Substitute(FSSource,"//VTK::Color::Dec", colorDec);
  vtkShaderProgram::Substitute(FSSource,"//VTK::Color::Impl", colorImpl);

  shaders[vtkShader::Vertex]->SetSource(VSSource);
  shaders[vtkShader::Geometry]->SetSource(GSSource);
  shaders[vtkShader::Fragment]->SetSource(FSSource);
//  shaders[vtkShader::TessControl]->SetSource(CSSource);
//  shaders[vtkShader::TessEvaluation]->SetSource(ESSource);
}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderLight(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *ren, vtkActor *actor)
{
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();

  // check for normal rendering
  vtkInformation *info = actor->GetPropertyKeys();
  if (info && info->Has(vtkLightingMapPass::RENDER_NORMALS()))
  {
      vtkShaderProgram::Substitute(FSSource,"//VTK::Light::Impl",
        "  vec3 n = (normalVCVSOutput + 1.0) * 0.5;\n"
        "  gl_FragData[0] = vec4(n.x, n.y, n.z, 1.0);"
      );
      shaders[vtkShader::Fragment]->SetSource(FSSource);
      return;
  }

  // If rendering, set diffuse and specular colors to pure white
  if (info && info->Has(vtkLightingMapPass::RENDER_LUMINANCE()))
  {
      vtkShaderProgram::Substitute(FSSource, "//VTK::Light::Impl",
        "  diffuseColor = vec3(1, 1, 1);\n"
        "  specularColor = vec3(1, 1, 1);\n"
        "  //VTK::Light::Impl\n",
        false
      );
  }

  // get Standard Lighting Decls
  vtkShaderProgram::Substitute(FSSource,"//VTK::Light::Dec",
    static_cast<vtkOpenGLRenderer *>(ren)->GetLightingUniforms());

  int lastLightComplexity = this->LastLightComplexity[this->LastBoundBO];
  int lastLightCount = this->LastLightCount[this->LastBoundBO];

  std::ostringstream toString;
  switch (lastLightComplexity)
  {
    case 0: // no lighting or RENDER_VALUES
      vtkShaderProgram::Substitute(FSSource, "//VTK::Light::Impl",
        "  gl_FragData[0] = vec4(ambientColor + diffuseColor, opacity);\n"
        "  //VTK::Light::Impl\n",
        false
        );
      break;

    case 1:  // headlight
      toString <<
        "  float df = max(0.0,normalVCVSOutput.z);\n"
        "  float sf = pow(df, specularPower);\n"
        "  vec3 diffuse = df * diffuseColor * lightColor0;\n"
        "  vec3 specular = sf * specularColor * lightColor0;\n"
        "  gl_FragData[0] = vec4(ambientColor + diffuse + specular, opacity);\n"
        "  //VTK::Light::Impl\n";
      vtkShaderProgram::Substitute(FSSource, "//VTK::Light::Impl",
        toString.str(), false);
      break;

    case 2: // light kit
      toString.clear();
      toString.str("");
      toString <<
        "  vec3 diffuse = vec3(0,0,0);\n"
        "  vec3 specular = vec3(0,0,0);\n"
        "  float df;\n"
        "  float sf;\n";
      for (int i = 0; i < lastLightCount; ++i)
      {
        toString <<
          "    df = max(0.0, dot(normalVCVSOutput, -lightDirectionVC" << i << "));\n"
          // if you change the next line also change vtkShadowMapPass
          "  diffuse += (df * lightColor" << i << ");\n" <<
          "  sf = sign(df)*pow(max(0.0, dot( reflect(lightDirectionVC" <<i <<
          ", normalVCVSOutput), normalize(-vertexVC.xyz))), specularPower);\n"
          // if you change the next line also change vtkShadowMapPass
          "  specular += (sf * lightColor" << i << ");\n";
      }
      toString <<
        "  diffuse = diffuse * diffuseColor;\n"
        "  specular = specular * specularColor;\n"
        "  gl_FragData[0] = vec4(ambientColor + diffuse + specular, opacity);"
        "  //VTK::Light::Impl";
      vtkShaderProgram::Substitute(FSSource,"//VTK::Light::Impl",
        toString.str(), false);
      break;

    case 3: // positional
      toString.clear();
      toString.str("");
      toString <<
        "  vec3 diffuse = vec3(0,0,0);\n"
        "  vec3 specular = vec3(0,0,0);\n"
        "  vec3 vertLightDirectionVC;\n"
        "  float attenuation;\n"
        "  float df;\n"
        "  float sf;\n"
        ;
      for (int i = 0; i < lastLightCount; ++i)
      {
        toString <<
          "    attenuation = 1.0;\n"
          "    if (lightPositional" << i << " == 0) {\n"
          "      vertLightDirectionVC = lightDirectionVC" << i << "; }\n"
          "    else {\n"
          "      vertLightDirectionVC = vertexVC.xyz - lightPositionVC" << i << ";\n"
          "      float distanceVC = length(vertLightDirectionVC);\n"
          "      vertLightDirectionVC = normalize(vertLightDirectionVC);\n"
          "      attenuation = 1.0 /\n"
          "        (lightAttenuation" << i << ".x\n"
          "         + lightAttenuation" << i << ".y * distanceVC\n"
          "         + lightAttenuation" << i << ".z * distanceVC * distanceVC);\n"
          "      // per OpenGL standard cone angle is 90 or less for a spot light\n"
          "      if (lightConeAngle" << i << " <= 90.0) {\n"
          "        float coneDot = dot(vertLightDirectionVC, lightDirectionVC" << i << ");\n"
          "        // if inside the cone\n"
          "        if (coneDot >= cos(radians(lightConeAngle" << i << "))) {\n"
          "          attenuation = attenuation * pow(coneDot, lightExponent" << i << "); }\n"
          "        else {\n"
          "          attenuation = 0.0; }\n"
          "        }\n"
          "      }\n" <<
          "    df = max(0.0,attenuation*dot(normalVCVSOutput, -vertLightDirectionVC));\n"
          // if you change the next line also change vtkShadowMapPass
          "    diffuse += (df * lightColor" << i << ");\n"
          "    sf = sign(df)*attenuation*pow( max(0.0, dot( reflect(vertLightDirectionVC, normalVCVSOutput), normalize(-vertexVC.xyz))), specularPower);\n"
          // if you change the next line also change vtkShadowMapPass
          "      specular += (sf * lightColor" << i << ");\n";
      }
      toString <<
        "  diffuse = diffuse * diffuseColor;\n"
        "  specular = specular * specularColor;\n"
        "  gl_FragData[0] = vec4(ambientColor + diffuse + specular, opacity);"
        "  //VTK::Light::Impl";
      vtkShaderProgram::Substitute(FSSource,"//VTK::Light::Impl",
        toString.str(), false);
      break;
  }

  // If rendering luminance values, write those values to the fragment
  if (info && info->Has(vtkLightingMapPass::RENDER_LUMINANCE()))
  {
    switch (this->LastLightComplexity[this->LastBoundBO])
    {
      case 0: // no lighting
        vtkShaderProgram::Substitute(FSSource, "//VTK::Light::Impl",
          "  gl_FragData[0] = vec4(0.0, 0.0, 0.0, 1.0);"
        );
        break;
      case 1: // headlight
      case 2: // light kit
      case 3: // positional
        vtkShaderProgram::Substitute(FSSource, "//VTK::Light::Impl",
          "  float ambientY = dot(vec3(0.2126, 0.7152, 0.0722), ambientColor);\n"
          "  gl_FragData[0] = vec4(ambientY, diffuse.x, specular.x, 1.0);"
        );
        break;
    }
  }

  shaders[vtkShader::Fragment]->SetSource(FSSource);
}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderTCoord(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *, vtkActor *actor)
{
  if (this->DrawingEdgesOrVertices)
  {
    return;
  }

  std::vector<texinfo> textures = this->GetTextures(actor);
  if (textures.empty())
  {
    return;
  }

  std::string VSSource = shaders[vtkShader::Vertex]->GetSource();
  std::string GSSource = shaders[vtkShader::Geometry]->GetSource();
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();
//  std::string CSSource = shaders[vtkShader::TessControl]->GetSource();
//  std::string ESSource = shaders[vtkShader::TessEvaluation]->GetSource();

  // always define texture maps if we have them
  std::string tMapDecFS;
  for (auto it : textures)
  {
    if (it.first->GetCubeMap())
    {
      tMapDecFS += "uniform samplerCube ";
    }
    else
    {
      tMapDecFS += "uniform sampler2D ";
    }
    tMapDecFS += it.second + ";\n";
  }
  vtkShaderProgram::Substitute(FSSource, "//VTK::TMap::Dec", tMapDecFS);

  // now handle each texture coordinate
  std::set<std::string> tcoordnames;
  for (auto it : textures)
  {
    // do we have special tcoords for this texture?
    std::string tcoordname = this->GetTextureCoordinateName(it.second.c_str());
    int tcoordComps = this->VBOs->GetNumberOfComponents(tcoordname.c_str());
    if (tcoordComps == 1 || tcoordComps == 2)
    {
      tcoordnames.insert(tcoordname);
    }
  }

  // if no texture coordinates then we are done
  if (tcoordnames.empty())
  {
    shaders[vtkShader::Vertex]->SetSource(VSSource);
    shaders[vtkShader::Geometry]->SetSource(GSSource);
    shaders[vtkShader::Fragment]->SetSource(FSSource);
//    shaders[vtkShader::TessControl]->SetSource(CSSource);
//    shaders[vtkShader::TessEvaluation]->SetSource(ESSource);
    return;
  }

  // handle texture transformation matrix and create the
  // vertex shader texture coordinate implementation
  // code for all texture coordinates.
  vtkInformation *info = actor->GetPropertyKeys();
  std::string vsimpl;
  if (info && info->Has(vtkProp::GeneralTextureTransform()))
  {
    vtkShaderProgram::Substitute(VSSource, "//VTK::TCoord::Dec",
      "//VTK::TCoord::Dec\n"
      "uniform mat4 tcMatrix;",
      false);
    for (const auto& it : tcoordnames)
    {
      int tcoordComps = this->VBOs->GetNumberOfComponents(it.c_str());
      if (tcoordComps == 1)
      {
        vsimpl = vsimpl + "vec4 " + it + "Tmp = tcMatrix*vec4(" + it + ",0.0,0.0,1.0);\n"
          + it + "VCVSOutput = " + it + "Tmp.x/" + it + "Tmp.w;\n";
      }
      else
      {
        vsimpl = vsimpl + "vec4 " + it + "Tmp = tcMatrix*vec4(" + it + ",0.0,1.0);\n"
          + it + "VCVSOutput = " + it + "Tmp.xy/" + it + "Tmp.w;\n";
      }
    }
  }
  else
  {
    for (const auto& it : tcoordnames)
    {
      vsimpl = vsimpl + it + "VCVSOutput = " + it + ";\n";
    }
  }
  vtkShaderProgram::Substitute(VSSource, "//VTK::TCoord::Impl", vsimpl);

  // now create the rest of the vertex and geometry shader code
  std::string vsdec;
  std::string gsdec;
  std::string gsimpl;
  std::string fsdec;
  for (const auto& it : tcoordnames)
  {
    int tcoordComps = this->VBOs->GetNumberOfComponents(it.c_str());
    std::string tCoordType;
    if (tcoordComps == 1)
    {
      tCoordType = "float";
    }
    else
    {
      tCoordType = "vec2";
    }
    vsdec = vsdec + "in " + tCoordType + " " + it + ";\n";
    vsdec = vsdec + "out " + tCoordType + " " + it + "VCVSOutput;\n";
    gsdec = gsdec + "in " + tCoordType + " " + it + "VCVSOutput[];\n";
    gsdec = gsdec + "out " + tCoordType + " " + it + "VCGSOutput;\n";
    gsimpl = gsimpl + it + "VCGSOutput = " + it + "VCVSOutput[i];\n";
    fsdec = fsdec + "in " + tCoordType + " " + it + "VCVSOutput;\n";
  }
  vtkShaderProgram::Substitute(VSSource, "//VTK::TCoord::Dec", vsdec);
  vtkShaderProgram::Substitute(GSSource, "//VTK::TCoord::Dec", gsdec);
  vtkShaderProgram::Substitute(GSSource, "//VTK::TCoord::Impl", gsimpl);
  vtkShaderProgram::Substitute(FSSource, "//VTK::TCoord::Dec", fsdec);

  // OK now handle the fragment shader implementation
  // everything else has been done.
  std::string tCoordImpFS;
  for (size_t i = 0; i < textures.size(); ++i)
  {
    vtkTexture *texture = textures[i].first;
    std::stringstream ss;

    // do we have special tcoords for this texture?
    std::string tcoordname = this->GetTextureCoordinateName(textures[i].second.c_str());
    int tcoordComps = this->VBOs->GetNumberOfComponents(tcoordname.c_str());

    std::string tCoordImpFSPre;
    std::string tCoordImpFSPost;
    if (tcoordComps == 1)
    {
      tCoordImpFSPre = "vec2(";
      tCoordImpFSPost = ", 0.0)";
    }
    else
    {
      tCoordImpFSPre = "";
      tCoordImpFSPost = "";
    }


    // Read texture color
    ss << "vec4 tcolor_" << i << " = texture(" << textures[i].second << ", "
       << tCoordImpFSPre << tcoordname << "VCVSOutput" << tCoordImpFSPost << "); // Read texture color\n";

    // Update color based on texture number of components
    int tNumComp = vtkOpenGLTexture::SafeDownCast(texture)->GetTextureObject()->GetComponents();
    switch (tNumComp)
    {
      case 1:
        ss << "tcolor_" << i << " = vec4(tcolor_" << i << ".r,tcolor_" << i << ".r,tcolor_" << i << ".r,1.0)";
        break;
      case 2:
        ss << "tcolor_" << i << " = vec4(tcolor_" << i << ".r,tcolor_" << i << ".r,tcolor_" << i << ".r,tcolor_" << i << ".g)";
        break;
      case 3:
        ss << "tcolor_" << i << " = vec4(tcolor_" << i << ".r,tcolor_" << i << ".g,tcolor_" << i << ".b,1.0)";
    }
    ss << "; // Update color based on texture nbr of components \n";

    // Define final color based on texture blending
    if(i == 0)
    {
      ss << "vec4 tcolor = tcolor_" << i << "; // BLENDING: None (first texture) \n\n";
    }
    else
    {
      int tBlending = vtkOpenGLTexture::SafeDownCast(texture)->GetBlendingMode();
      switch (tBlending)
      {
        case vtkTexture::VTK_TEXTURE_BLENDING_MODE_REPLACE:
          ss << "tcolor.rgb = tcolor_" << i << ".rgb * tcolor_" << i << ".a + "
             << "tcolor.rgb * (1 - tcolor_" << i << " .a); // BLENDING: Replace\n"
             << "tcolor.a = tcolor_" << i << ".a + tcolor.a * (1 - tcolor_" << i << " .a); // BLENDING: Replace\n\n";
          break;
        case vtkTexture::VTK_TEXTURE_BLENDING_MODE_MODULATE:
          ss << "tcolor *= tcolor_" << i << "; // BLENDING: Modulate\n\n";
          break;
        case vtkTexture::VTK_TEXTURE_BLENDING_MODE_ADD:
          ss << "tcolor.rgb = tcolor_" << i << ".rgb * tcolor_" << i << ".a + "
             << "tcolor.rgb * tcolor.a; // BLENDING: Add\n"
             << "tcolor.a += tcolor_" << i << ".a; // BLENDING: Add\n\n";
          break;
        case vtkTexture::VTK_TEXTURE_BLENDING_MODE_ADD_SIGNED:
          ss << "tcolor.rgb = tcolor_" << i << ".rgb * tcolor_" << i << ".a + "
             << "tcolor.rgb * tcolor.a - 0.5; // BLENDING: Add signed\n"
             << "tcolor.a += tcolor_" << i << ".a - 0.5; // BLENDING: Add signed\n\n";
          break;
        case vtkTexture::VTK_TEXTURE_BLENDING_MODE_INTERPOLATE:
          vtkDebugMacro(<< "Interpolate blending mode not supported for OpenGL2 backend.");
          break;
        case vtkTexture::VTK_TEXTURE_BLENDING_MODE_SUBTRACT:
          ss << "tcolor.rgb -= tcolor_" << i << ".rgb * tcolor_" << i << ".a; // BLENDING: Subtract\n\n";
          break;
        default:
          vtkDebugMacro(<< "No blending mode given, ignoring this texture colors.");
          ss << "// NO BLENDING MODE: ignoring this texture colors\n";
      }
    }
    tCoordImpFS += ss.str();
  }

  // do texture mapping except for scalar coloring case which is
  // handled in the scalar coloring code
  if (!this->InterpolateScalarsBeforeMapping || !this->ColorCoordinates)
  {
    vtkShaderProgram::Substitute(FSSource, "//VTK::TCoord::Impl",
      tCoordImpFS + "gl_FragData[0] = gl_FragData[0] * tcolor;");
  }

  shaders[vtkShader::Vertex]->SetSource(VSSource);
  shaders[vtkShader::Geometry]->SetSource(GSSource);
  shaders[vtkShader::Fragment]->SetSource(FSSource);
//  shaders[vtkShader::TessControl]->SetSource(CSSource);
//  shaders[vtkShader::TessEvaluation]->SetSource(ESSource);

}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderPicking(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer * /* ren */, vtkActor *)
{
  // process actor composite low mid high
  std::string VSSource = shaders[vtkShader::Vertex]->GetSource();
  std::string GSSource = shaders[vtkShader::Geometry]->GetSource();
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();
//  std::string CSSource = shaders[vtkShader::TessControl]->GetSource();
//  std::string ESSource = shaders[vtkShader::TessEvaluation]->GetSource();


  if (this->LastSelectionState >= vtkHardwareSelector::MIN_KNOWN_PASS)
  {
    switch (this->LastSelectionState)
    {
      // point ID low and high are always just gl_VertexId
      case vtkHardwareSelector::POINT_ID_LOW24:
        vtkShaderProgram::Substitute(VSSource,
          "//VTK::Picking::Dec",
          "flat out int vertexIDVSOutput;\n");
        vtkShaderProgram::Substitute(VSSource,
          "//VTK::Picking::Impl",
          "  vertexIDVSOutput = gl_VertexID;\n");
        vtkShaderProgram::Substitute(GSSource,
          "//VTK::Picking::Dec",
          "flat in int vertexIDVSOutput[];\n"
          "flat out int vertexIDGSOutput;");
        vtkShaderProgram::Substitute(GSSource,
          "//VTK::Picking::Impl",
          "vertexIDGSOutput = vertexIDVSOutput[i];");
        vtkShaderProgram::Substitute(FSSource,
          "//VTK::Picking::Dec",
          "flat in int vertexIDVSOutput;\n");
        vtkShaderProgram::Substitute(FSSource,
        "//VTK::Picking::Impl",
        "  int idx = vertexIDVSOutput + 1;\n"
        "  gl_FragData[0] = vec4(float(idx%256)/255.0, float((idx/256)%256)/255.0, float((idx/65536)%256)/255.0, 1.0);\n");
        break;

      case vtkHardwareSelector::POINT_ID_HIGH24:
        // this may yerk on openGL ES 2.0 so no really huge meshes in ES 2.0 OK
        vtkShaderProgram::Substitute(VSSource,
          "//VTK::Picking::Dec",
          "flat out int vertexIDVSOutput;\n");
        vtkShaderProgram::Substitute(VSSource,
          "//VTK::Picking::Impl",
          "  vertexIDVSOutput = gl_VertexID;\n");
        vtkShaderProgram::Substitute(GSSource,
          "//VTK::Picking::Dec",
          "flat in int vertexIDVSOutput[];\n"
          "flat out int vertexIDGSOutput;");
        vtkShaderProgram::Substitute(GSSource,
          "//VTK::Picking::Impl",
          "vertexIDGSOutput = vertexIDVSOutput[i];");
        vtkShaderProgram::Substitute(FSSource,
          "//VTK::Picking::Dec",
          "flat in int vertexIDVSOutput;\n");
        vtkShaderProgram::Substitute(FSSource,
        "//VTK::Picking::Impl",
        "  int idx = (vertexIDVSOutput + 1);\n idx = ((idx & 0xff000000) >> 24);\n"
        "  gl_FragData[0] = vec4(float(idx%256)/255.0, float((idx/256)%256)/255.0, float(idx/65536)/255.0, 1.0);\n");
        break;

      // cell ID is just gl_PrimitiveID
      case vtkHardwareSelector::CELL_ID_LOW24:
        vtkShaderProgram::Substitute(FSSource,
        "//VTK::Picking::Impl",
        "  int idx = gl_PrimitiveID + 1 + PrimitiveIDOffset;\n"
        "  gl_FragData[0] = vec4(float(idx%256)/255.0, float((idx/256)%256)/255.0, float((idx/65536)%256)/255.0, 1.0);\n");
        break;

      case vtkHardwareSelector::CELL_ID_HIGH24:
        // this may yerk on openGL ES 2.0 so no really huge meshes in ES 2.0 OK
        // if (selector &&
        //     selector->GetFieldAssociation() == vtkDataObject::FIELD_ASSOCIATION_POINTS)
        vtkShaderProgram::Substitute(FSSource,
        "//VTK::Picking::Impl",
        "  int idx = (gl_PrimitiveID + 1 + PrimitiveIDOffset);\n idx = ((idx & 0xff000000) >> 24);\n"
        "  gl_FragData[0] = vec4(float(idx%256)/255.0, float((idx/256)%256)/255.0, float(idx/65536)/255.0, 1.0);\n");
        break;

      default: // actor process and composite
        vtkShaderProgram::Substitute(FSSource, "//VTK::Picking::Dec",
          "uniform vec3 mapperIndex;");
        vtkShaderProgram::Substitute(FSSource,
          "//VTK::Picking::Impl",
          "  gl_FragData[0] = vec4(mapperIndex,1.0);\n");
    }
  }
  shaders[vtkShader::Vertex]->SetSource(VSSource);
  shaders[vtkShader::Geometry]->SetSource(GSSource);
  shaders[vtkShader::Fragment]->SetSource(FSSource);
//  shaders[vtkShader::TessControl]->SetSource(CSSource);
//  shaders[vtkShader::TessEvaluation]->SetSource(ESSource);

}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderClip(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *, vtkActor *)
{
  std::string VSSource = shaders[vtkShader::Vertex]->GetSource();
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();
  std::string GSSource = shaders[vtkShader::Geometry]->GetSource();
//  std::string CSSource = shaders[vtkShader::TessControl]->GetSource();
//  std::string ESSource = shaders[vtkShader::TessEvaluation]->GetSource();


  if (this->GetNumberOfClippingPlanes())
  {
    // add all the clipping planes
    int numClipPlanes = this->GetNumberOfClippingPlanes();
    if (numClipPlanes > 6)
    {
      vtkErrorMacro(<< "OpenGL has a limit of 6 clipping planes");
    }

    // geometry shader impl
    if (GSSource.length())
    {
      vtkShaderProgram::Substitute(VSSource, "//VTK::Clip::Dec",
        "out vec4 clipVertexMC;");
      vtkShaderProgram::Substitute(VSSource, "//VTK::Clip::Impl",
        "  clipVertexMC =  vertexMC;\n");
      vtkShaderProgram::Substitute(GSSource,
        "//VTK::Clip::Dec",
        "uniform int numClipPlanes;\n"
        "uniform vec4 clipPlanes[6];\n"
        "in vec4 clipVertexMC[];\n"
        "out float clipDistancesGSOutput[6];");
      vtkShaderProgram::Substitute(GSSource,
        "//VTK::Clip::Impl",
        "for (int planeNum = 0; planeNum < numClipPlanes; planeNum++)\n"
        "  {\n"
        "    clipDistancesGSOutput[planeNum] = dot(clipPlanes[planeNum], clipVertexMC[i]);\n"
        "  }\n");
    }
    else // vertex shader impl
    {
      vtkShaderProgram::Substitute(VSSource, "//VTK::Clip::Dec",
        "uniform int numClipPlanes;\n"
        "uniform vec4 clipPlanes[6];\n"
        "out float clipDistancesVSOutput[6];");
      vtkShaderProgram::Substitute(VSSource, "//VTK::Clip::Impl",
        "for (int planeNum = 0; planeNum < numClipPlanes; planeNum++)\n"
        "    {\n"
        "    clipDistancesVSOutput[planeNum] = dot(clipPlanes[planeNum], vertexMC);\n"
        "    }\n");
    }

    vtkShaderProgram::Substitute(FSSource, "//VTK::Clip::Dec",
      "uniform int numClipPlanes;\n"
      "in float clipDistancesVSOutput[6];");
    vtkShaderProgram::Substitute(FSSource, "//VTK::Clip::Impl",
      "for (int planeNum = 0; planeNum < numClipPlanes; planeNum++)\n"
      "    {\n"
      "    if (clipDistancesVSOutput[planeNum] < 0.0) discard;\n"
      "    }\n");
  }
  shaders[vtkShader::Vertex]->SetSource(VSSource);
  shaders[vtkShader::Fragment]->SetSource(FSSource);
  shaders[vtkShader::Geometry]->SetSource(GSSource);
//  shaders[vtkShader::TessControl]->SetSource(CSSource);
//  shaders[vtkShader::TessEvaluation]->SetSource(ESSource);
}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderNormal(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *, vtkActor *actor)
{
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();

//  std::string CSSource = shaders[vtkShader::TessControl]->GetSource();
//  std::string ESSource = shaders[vtkShader::TessEvaluation]->GetSource();

//  shaders[vtkShader::TessControl]->SetSource(CSSource);
//  shaders[vtkShader::TessEvaluation]->SetSource(ESSource);


  // Render points as spheres if so requested
  // To get the correct zbuffer values we have to
  // adjust the incoming z value based on the shape
  // of the sphere, See the document
  // PixelsToZBufferConversion in this directory for
  // the derivation of the equations used.
  if (this->DrawingSpheres(*this->LastBoundBO, actor))
  {
    vtkShaderProgram::Substitute(FSSource,
      "//VTK::Normal::Dec",
      "uniform float ZCalcS;\n"
      "uniform float ZCalcR;\n"
      );
    vtkShaderProgram::Substitute(FSSource,
      "//VTK::Normal::Impl",

      " float xpos = 2.0*gl_PointCoord.x - 1.0;\n"
      " float ypos = 1.0 - 2.0*gl_PointCoord.y;\n"
      " float len2 = xpos*xpos+ ypos*ypos;\n"
      " if (len2 > 1.0) { discard; }\n"
      " vec3 normalVCVSOutput = normalize(\n"
      "   vec3(2.0*gl_PointCoord.x - 1.0, 1.0 - 2.0*gl_PointCoord.y, sqrt(1.0 - len2)));\n"

      " gl_FragDepth = gl_FragCoord.z + normalVCVSOutput.z*ZCalcS*ZCalcR;\n"
      " if (cameraParallel == 0) {\n"
      "  float ZCalcQ = (normalVCVSOutput.z*ZCalcR - 1.0);\n"
      "  gl_FragDepth = (ZCalcS - gl_FragCoord.z) / ZCalcQ + ZCalcS; }\n"
      );

     shaders[vtkShader::Fragment]->SetSource(FSSource);
     return;
  }

  // Render lines as tubes if so requested
  // To get the correct zbuffer values we have to
  // adjust the incoming z value based on the shape
  // of the tube, See the document
  // PixelsToZBufferConversion in this directory for
  // the derivation of the equations used.

  // note these are not real tubes. They are wide
  // lines that are fudged a bit to look like tubes
  // this approach is simpler than the OpenGLStickMapper
  // but results in things that are not really tubes
  // for best results use points as spheres with
  // these tubes and make sure the point Width is
  // twice the tube width
  if (this->DrawingTubes(*this->LastBoundBO, actor))
  {
    std::string GSSource = shaders[vtkShader::Geometry]->GetSource();

    vtkShaderProgram::Substitute(FSSource,
      "//VTK::Normal::Dec",
      "in vec3 tubeBasis1;\n"
      "in vec3 tubeBasis2;\n"
      "uniform float ZCalcS;\n"
      "uniform float ZCalcR;\n"
      );
    vtkShaderProgram::Substitute(FSSource,
      "//VTK::Normal::Impl",

      "float len2 = tubeBasis1.x*tubeBasis1.x + tubeBasis1.y*tubeBasis1.y;\n"
      "float lenZ = clamp(sqrt(1.0 - len2),0.0,1.0);\n"
      "vec3 normalVCVSOutput = normalize(tubeBasis1 + tubeBasis2*lenZ);\n"
      " gl_FragDepth = gl_FragCoord.z + lenZ*ZCalcS*ZCalcR/clamp(tubeBasis2.z,0.5,1.0);\n"
      " if (cameraParallel == 0) {\n"
      "  float ZCalcQ = (lenZ*ZCalcR/clamp(tubeBasis2.z,0.5,1.0) - 1.0);\n"
      "  gl_FragDepth = (ZCalcS - gl_FragCoord.z) / ZCalcQ + ZCalcS; }\n"
      );

    vtkShaderProgram::Substitute(GSSource,
      "//VTK::Normal::Dec",
      "in vec4 vertexVCVSOutput[];\n"
      "out vec3 tubeBasis1;\n"
      "out vec3 tubeBasis2;\n"
      );

    vtkShaderProgram::Substitute(GSSource,
      "//VTK::Normal::Start",
      "vec3 lineDir = normalize(vertexVCVSOutput[1].xyz - vertexVCVSOutput[0].xyz);\n"
      "tubeBasis2 = normalize(cross(lineDir, vec3(normal, 0.0)));\n"
      "tubeBasis2 = tubeBasis2*sign(tubeBasis2.z);\n"
      );

    vtkShaderProgram::Substitute(GSSource,
      "//VTK::Normal::Impl",
      "tubeBasis1 = 2.0*vec3(normal*((j+1)%2 - 0.5), 0.0);\n"
      );

    shaders[vtkShader::Geometry]->SetSource(GSSource);
    shaders[vtkShader::Fragment]->SetSource(FSSource);
    return;
  }

  if (this->LastLightComplexity[this->LastBoundBO] > 0)
  {
    std::string VSSource = shaders[vtkShader::Vertex]->GetSource();
    std::string GSSource = shaders[vtkShader::Geometry]->GetSource();

    // if we have point normals provided
    if (this->VBOs->GetNumberOfComponents("normalMC") == 3)
    {
      vtkShaderProgram::Substitute(VSSource,
        "//VTK::Normal::Dec",
        "in vec3 normalMC;\n"
        "uniform mat3 normalMatrix;\n"
        "out vec3 normalVCVSOutput;");
      vtkShaderProgram::Substitute(VSSource,
        "//VTK::Normal::Impl",
        "normalVCVSOutput = normalMatrix * normalMC;");
      vtkShaderProgram::Substitute(GSSource,
        "//VTK::Normal::Dec",
        "in vec3 normalVCVSOutput[];\n"
        "out vec3 normalVCGSOutput;");
      vtkShaderProgram::Substitute(GSSource,
        "//VTK::Normal::Impl",
        "normalVCGSOutput = normalVCVSOutput[i];");
      vtkShaderProgram::Substitute(FSSource,
        "//VTK::Normal::Dec",
        "in vec3 normalVCVSOutput;");
      vtkShaderProgram::Substitute(FSSource,
        "//VTK::Normal::Impl",
        "vec3 normalVCVSOutput = normalize(normalVCVSOutput);\n"
        //  if (!gl_FrontFacing) does not work in intel hd4000 mac
        //  if (int(gl_FrontFacing) == 0) does not work on mesa
        "  if (gl_FrontFacing == false) { normalVCVSOutput = -normalVCVSOutput; }\n"
        //"normalVC = normalVCVarying;"
        );

      shaders[vtkShader::Vertex]->SetSource(VSSource);
      shaders[vtkShader::Geometry]->SetSource(GSSource);
      shaders[vtkShader::Fragment]->SetSource(FSSource);
      return;
    }

    // OK no point normals, how about cell normals
    if (this->HaveCellNormals)
    {
      vtkShaderProgram::Substitute(FSSource,
        "//VTK::Normal::Dec",
        "uniform mat3 normalMatrix;\n"
        "uniform samplerBuffer textureN;\n");
      if (this->CellNormalTexture->GetVTKDataType() == VTK_FLOAT)
      {
        vtkShaderProgram::Substitute(FSSource,
          "//VTK::Normal::Impl",
          "vec3 normalVCVSOutput = \n"
          "    texelFetchBuffer(textureN, gl_PrimitiveID + PrimitiveIDOffset).xyz;\n"
          "normalVCVSOutput = normalize(normalMatrix * normalVCVSOutput);\n"
          "  if (gl_FrontFacing == false) { normalVCVSOutput = -normalVCVSOutput; }\n"
          );
      }
      else
      {
        vtkShaderProgram::Substitute(FSSource,
            "//VTK::Normal::Impl",
            "vec3 normalVCVSOutput = \n"
            "    texelFetchBuffer(textureN, gl_PrimitiveID + PrimitiveIDOffset).xyz;\n"
            "normalVCVSOutput = normalVCVSOutput * 255.0/127.0 - 1.0;\n"
            "normalVCVSOutput = normalize(normalMatrix * normalVCVSOutput);\n"
            "  if (gl_FrontFacing == false) { normalVCVSOutput = -normalVCVSOutput; }\n"
            );
        shaders[vtkShader::Fragment]->SetSource(FSSource);
        return;
      }
    }

    // OK we have no point or cell normals, so compute something
    // we have a formula for wireframe
    if (actor->GetProperty()->GetRepresentation() == VTK_WIREFRAME)
    {
      // generate a normal for lines, it will be perpendicular to the line
      // and maximally aligned with the camera view direction
      // no clue if this is the best way to do this.
      // the code below has been optimized a bit so what follows is
      // an explanation of the basic approach. Compute the gradient of the line
      // with respect to x and y, the larger of the two
      // cross that with the camera view direction. That gives a vector
      // orthogonal to the camera view and the line. Note that the line and the camera
      // view are probably not orthogonal. Which is why when we cross result that with
      // the line gradient again we get a reasonable normal. It will be othogonal to
      // the line (which is a plane but maximally aligned with the camera view.
      vtkShaderProgram::Substitute(
            FSSource,"//VTK::UniformFlow::Impl",
            "  vec3 fdx = vec3(dFdx(vertexVC.x),dFdx(vertexVC.y),dFdx(vertexVC.z));\n"
            "  vec3 fdy = vec3(dFdy(vertexVC.x),dFdy(vertexVC.y),dFdy(vertexVC.z));\n"
            // the next two lines deal with some rendering systems
            // that have difficulty computing dfdx/dfdy when they
            // are near zero. Normalization later on can amplify
            // the issue causing rendering artifacts.
            "  if (abs(fdx.x) < 0.000001) { fdx = vec3(0.0);}\n"
            "  if (abs(fdy.y) < 0.000001) { fdy = vec3(0.0);}\n"
            "  //VTK::UniformFlow::Impl\n" // For further replacements
            );
      vtkShaderProgram::Substitute(FSSource,"//VTK::Normal::Impl",
        "vec3 normalVCVSOutput;\n"
        "  fdx = normalize(fdx);\n"
        "  fdy = normalize(fdy);\n"
        "  if (abs(fdx.x) > 0.0)\n"
        "    { normalVCVSOutput = normalize(cross(vec3(fdx.y, -fdx.x, 0.0), fdx)); }\n"
        "  else { normalVCVSOutput = normalize(cross(vec3(fdy.y, -fdy.x, 0.0), fdy));}"
        );
    }
    else // not lines, so surface
    {
      vtkShaderProgram::Substitute(
            FSSource,"//VTK::UniformFlow::Impl",
            "vec3 fdx = vec3(dFdx(vertexVC.x),dFdx(vertexVC.y),dFdx(vertexVC.z));\n"
            "  vec3 fdy = vec3(dFdy(vertexVC.x),dFdy(vertexVC.y),dFdy(vertexVC.z));\n"
            "  //VTK::UniformFlow::Impl\n" // For further replacements
            );
      vtkShaderProgram::Substitute(FSSource,"//VTK::Normal::Impl",
        "fdx = normalize(fdx);\n"
        "  fdy = normalize(fdy);\n"
        "  vec3 normalVCVSOutput = normalize(cross(fdx,fdy));\n"
        // the code below is faster, but does not work on some devices
        //"vec3 normalVC = normalize(cross(dFdx(vertexVC.xyz), dFdy(vertexVC.xyz)));\n"
        "  if (cameraParallel == 1 && normalVCVSOutput.z < 0.0) { normalVCVSOutput = -1.0*normalVCVSOutput; }\n"
        "  if (cameraParallel == 0 && dot(normalVCVSOutput,vertexVC.xyz) > 0.0) { normalVCVSOutput = -1.0*normalVCVSOutput; }"
        );
    }
    shaders[vtkShader::Fragment]->SetSource(FSSource);
  }
}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderPositionVC(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *, vtkActor *)
{
  std::string VSSource = shaders[vtkShader::Vertex]->GetSource();
  std::string GSSource = shaders[vtkShader::Geometry]->GetSource();
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();

//  std::string CSSource = shaders[vtkShader::TessControl]->GetSource();
//  std::string ESSource = shaders[vtkShader::TessEvaluation]->GetSource();

//  shaders[vtkShader::TessControl]->SetSource(CSSource);
//  shaders[vtkShader::TessEvaluation]->SetSource(ESSource);


  vtkShaderProgram::Substitute(FSSource,
    "//VTK::Camera::Dec",
    "uniform int cameraParallel;\n",
    false);

 // do we need the vertex in the shader in View Coordinates
  if (this->LastLightComplexity[this->LastBoundBO] > 0)
  {
    vtkShaderProgram::Substitute(VSSource,
      "//VTK::PositionVC::Dec",
      "out vec4 vertexVCVSOutput;");
    vtkShaderProgram::Substitute(VSSource,
      "//VTK::PositionVC::Impl",
      "vertexVCVSOutput = MCVCMatrix * vertexMC;\n"
      "  gl_Position = MCDCMatrix * vertexMC;\n");
    vtkShaderProgram::Substitute(VSSource,
      "//VTK::Camera::Dec",
      "uniform mat4 MCDCMatrix;\n"
      "uniform mat4 MCVCMatrix;");
    vtkShaderProgram::Substitute(GSSource,
      "//VTK::PositionVC::Dec",
      "in vec4 vertexVCVSOutput[];\n"
      "out vec4 vertexVCGSOutput;");
    vtkShaderProgram::Substitute(GSSource,
      "//VTK::PositionVC::Impl",
      "vertexVCGSOutput = vertexVCVSOutput[i];");
    vtkShaderProgram::Substitute(FSSource,
      "//VTK::PositionVC::Dec",
      "in vec4 vertexVCVSOutput;");
    vtkShaderProgram::Substitute(FSSource,
      "//VTK::PositionVC::Impl",
      "vec4 vertexVC = vertexVCVSOutput;");
  }
  else
  {
    vtkShaderProgram::Substitute(VSSource,
      "//VTK::Camera::Dec",
      "uniform mat4 MCDCMatrix;");
    vtkShaderProgram::Substitute(VSSource,
      "//VTK::PositionVC::Impl",
      "  gl_Position = MCDCMatrix * vertexMC;\n");
  }
  shaders[vtkShader::Vertex]->SetSource(VSSource);
  shaders[vtkShader::Geometry]->SetSource(GSSource);
  shaders[vtkShader::Fragment]->SetSource(FSSource);
}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderPrimID(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *, vtkActor *)
{
  std::string VSSource = shaders[vtkShader::Vertex]->GetSource();
  std::string GSSource = shaders[vtkShader::Geometry]->GetSource();
  std::string FSSource = shaders[vtkShader::Fragment]->GetSource();

//  std::string CSSource = shaders[vtkShader::TessControl]->GetSource();
//  std::string ESSource = shaders[vtkShader::TessEvaluation]->GetSource();
//  shaders[vtkShader::TessControl]->SetSource(CSSource);
//  shaders[vtkShader::TessEvaluation]->SetSource(ESSource);


  // are we handling the apple bug?
  if (!this->AppleBugPrimIDs.empty())
  {
    vtkShaderProgram::Substitute(VSSource,"//VTK::PrimID::Dec",
      "in vec4 appleBugPrimID;\n"
      "out vec4 applePrimIDVSOutput;");
    vtkShaderProgram::Substitute(VSSource,"//VTK::PrimID::Impl",
      "applePrimIDVSOutput = appleBugPrimID;");
    vtkShaderProgram::Substitute(GSSource,
      "//VTK::PrimID::Dec",
      "in  vec4 applePrimIDVSOutput[];\n"
      "out vec4 applePrimIDGSOutput;");
    vtkShaderProgram::Substitute(GSSource,
      "//VTK::PrimID::Impl",
      "applePrimIDGSOutput = applePrimIDVSOutput[i];");
    vtkShaderProgram::Substitute(FSSource,"//VTK::PrimID::Dec",
      "in vec4 applePrimIDVSOutput;");
     vtkShaderProgram::Substitute(FSSource,"//VTK::PrimID::Impl",
       "int vtkPrimID = int(applePrimIDVSOutput[0]*255.1) + int(applePrimIDVSOutput[1]*255.1)*256 + int(applePrimIDVSOutput[2]*255.1)*65536;");
    vtkShaderProgram::Substitute(FSSource,"gl_PrimitiveID","vtkPrimID");
  }
  else
  {
    vtkShaderProgram::Substitute(GSSource,
      "//VTK::PrimID::Impl",
      "gl_PrimitiveID = gl_PrimitiveIDIn;");
  }
  shaders[vtkShader::Vertex]->SetSource(VSSource);
  shaders[vtkShader::Geometry]->SetSource(GSSource);
  shaders[vtkShader::Fragment]->SetSource(FSSource);
}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderCoincidentOffset(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *ren, vtkActor *actor)
{
  float factor = 0.0;
  float offset = 0.0;
  this->GetCoincidentParameters(ren, actor,factor,offset);

  // if we need an offset handle it here
  // The value of .000016 is suitable for depth buffers
  // of at least 16 bit depth. We do not query the depth
  // right now because we would need some mechanism to
  // cache the result taking into account FBO changes etc.
  if (factor != 0.0 || offset != 0.0)
  {
    std::string FSSource = shaders[vtkShader::Fragment]->GetSource();

    if (factor != 0.0)
    {
      vtkShaderProgram::Substitute(FSSource,
        "//VTK::Coincident::Dec",
        "uniform float cfactor;\n"
        "uniform float coffset;");
      vtkShaderProgram::Substitute(FSSource,
        "//VTK::UniformFlow::Impl",
        "float cscale = length(vec2(dFdx(gl_FragCoord.z),dFdy(gl_FragCoord.z)));\n"
        "  //VTK::UniformFlow::Impl\n" // for other replacements
        );
      vtkShaderProgram::Substitute(FSSource, "//VTK::Depth::Impl",
        "gl_FragDepth = gl_FragCoord.z + cfactor*cscale + 0.000016*coffset;\n");
    }
    else
    {
      vtkShaderProgram::Substitute(FSSource,
        "//VTK::Coincident::Dec",
        "uniform float coffset;");
      vtkShaderProgram::Substitute(FSSource,
        "//VTK::Depth::Impl",
        "gl_FragDepth = gl_FragCoord.z + 0.000016*coffset;\n"
        );
    }
    shaders[vtkShader::Fragment]->SetSource(FSSource);
  }
}

// If MSAA is enabled, don't write to gl_FragDepth unless we absolutely have
// to. See VTK issue 16899.
void NMVtkOpenGLPolyDataMapper::ReplaceShaderDepth(
    std::map<vtkShader::Type, vtkShader *> ,
    vtkRenderer *,
    vtkActor *)
{
  // noop by default
}

void NMVtkOpenGLPolyDataMapper::ReplaceShaderValues(
  std::map<vtkShader::Type, vtkShader *> shaders,
  vtkRenderer *ren, vtkActor *actor)
{
  this->ReplaceShaderRenderPass(shaders, ren, actor, true);
  this->ReplaceShaderColor(shaders, ren, actor);
  this->ReplaceShaderNormal(shaders, ren, actor);
  this->ReplaceShaderLight(shaders, ren, actor);
  this->ReplaceShaderTCoord(shaders, ren, actor);
  this->ReplaceShaderPicking(shaders, ren, actor);
  this->ReplaceShaderClip(shaders, ren, actor);
  this->ReplaceShaderPrimID(shaders, ren, actor);
  this->ReplaceShaderPositionVC(shaders, ren, actor);
  this->ReplaceShaderCoincidentOffset(shaders, ren, actor);
  this->ReplaceShaderDepth(shaders, ren, actor);
  this->ReplaceShaderRenderPass(shaders, ren, actor, false);

//  cout << "VS: " << shaders[vtkShader::Vertex]->GetSource() << endl;
//  cout << "CS: " << shaders[vtkShader::TessControl]->GetSource() << endl;
//  cout << "ES: " << shaders[vtkShader::TessEvaluation]->GetSource() << endl;
//  cout << "GS: " << shaders[vtkShader::Geometry]->GetSource() << endl;
//  cout << "FS: " << shaders[vtkShader::Fragment]->GetSource() << endl;

  int b=3;
}

bool NMVtkOpenGLPolyDataMapper::DrawingTubesOrSpheres(
  vtkOpenGLHelper &cellBO, vtkActor *actor)
{
  unsigned int mode =
    this->GetOpenGLMode(actor->GetProperty()->GetRepresentation(),
      cellBO.PrimitiveType);
  vtkProperty*prop=actor->GetProperty();

  return (prop->GetRenderPointsAsSpheres() && mode == GL_POINTS) ||
     (prop->GetRenderLinesAsTubes() && mode == GL_LINES && prop->GetLineWidth() > 1.0);
}

bool NMVtkOpenGLPolyDataMapper::DrawingSpheres(vtkOpenGLHelper &cellBO, vtkActor *actor)
{
  return (actor->GetProperty()->GetRenderPointsAsSpheres() &&
    this->GetOpenGLMode(actor->GetProperty()->GetRepresentation(),
      cellBO.PrimitiveType) == GL_POINTS);
}

bool NMVtkOpenGLPolyDataMapper::DrawingTubes(vtkOpenGLHelper &cellBO, vtkActor *actor)
{
  return (actor->GetProperty()->GetRenderLinesAsTubes() &&
    actor->GetProperty()->GetLineWidth() > 1.0 &&
    this->GetOpenGLMode(actor->GetProperty()->GetRepresentation(),
      cellBO.PrimitiveType) == GL_LINES);
}

//-----------------------------------------------------------------------------
bool NMVtkOpenGLPolyDataMapper::GetNeedToRebuildShaders(
  vtkOpenGLHelper &cellBO, vtkRenderer* ren, vtkActor *actor)
{
  int lightComplexity = 0;
  int numberOfLights = 0;

  // wacky backwards compatibility with old VTK lighting
  // soooo there are many factors that determine if a primitive is lit or not.
  // three that mix in a complex way are representation POINT, Interpolation FLAT
  // and having normals or not.
  bool needLighting = false;
  bool haveNormals = (this->CurrentInput->GetPointData()->GetNormals() != nullptr);
  if (actor->GetProperty()->GetRepresentation() == VTK_POINTS)
  {
    needLighting = (actor->GetProperty()->GetInterpolation() != VTK_FLAT && haveNormals);
  }
  else  // wireframe or surface rep
  {
    bool isTrisOrStrips =
      (cellBO.PrimitiveType == PrimitiveTris || cellBO.PrimitiveType == PrimitiveTriStrips);
    needLighting = (isTrisOrStrips ||
      (!isTrisOrStrips && actor->GetProperty()->GetInterpolation() != VTK_FLAT && haveNormals));
  }

  // we sphering or tubing? Yes I made sphere into a verb
  if (this->DrawingTubesOrSpheres(cellBO, actor))
  {
    needLighting = true;
  }

  // do we need lighting?
  if (actor->GetProperty()->GetLighting() && needLighting)
  {
    vtkOpenGLRenderer *oren = static_cast<vtkOpenGLRenderer *>(ren);
    lightComplexity = oren->GetLightingComplexity();
    numberOfLights = oren->GetLightingCount();
  }

  if (this->LastLightComplexity[&cellBO] != lightComplexity ||
      this->LastLightCount[&cellBO] != numberOfLights)
  {
    this->LightComplexityChanged[&cellBO].Modified();
    this->LastLightComplexity[&cellBO] = lightComplexity;
    this->LastLightCount[&cellBO] = numberOfLights;
  }

  // has something changed that would require us to recreate the shader?
  // candidates are
  // -- property modified (representation interpolation and lighting)
  // -- input modified if it changes the presence of normals/tcoords
  // -- light complexity changed
  // -- any render pass that requires it
  // -- some selection state changes
  // we do some quick simple tests first

  // Have the renderpasses changed?
  vtkMTimeType renderPassMTime = this->GetRenderPassStageMTime(actor);

  // shape of input data changed?
  float factor, offset;
  this->GetCoincidentParameters(ren, actor, factor, offset);
  unsigned int scv
    = (this->CurrentInput->GetPointData()->GetNormals() ? 0x01 : 0)
    + (this->HaveCellScalars ? 0x02 : 0)
    + (this->HaveCellNormals ? 0x04 : 0)
    + ((factor != 0.0) ? 0x08 : 0)
    + ((offset != 0.0) ? 0x10 : 0)
    + (this->VBOs->GetNumberOfComponents("scalarColor") ? 0x20 : 0)
    + ((this->VBOs->GetNumberOfComponents("tcoord") % 4) << 6);

  if (cellBO.Program == nullptr ||
      cellBO.ShaderSourceTime < this->GetMTime() ||
      cellBO.ShaderSourceTime < actor->GetProperty()->GetMTime() ||
      cellBO.ShaderSourceTime < this->LightComplexityChanged[&cellBO] ||
      cellBO.ShaderSourceTime < this->SelectionStateChanged ||
      cellBO.ShaderSourceTime < renderPassMTime ||
      cellBO.ShaderChangeValue != scv)
  {
    cellBO.ShaderChangeValue = scv;
    return true;
  }

  // if texturing then texture components/blend funcs may have changed
  if (this->VBOs->GetNumberOfComponents("tcoord"))
  {
    vtkMTimeType texMTime = 0;
    std::vector<texinfo> textures = this->GetTextures(actor);
    for (size_t i = 0; i < textures.size(); ++i)
    {
      vtkTexture *texture = textures[i].first;
      texMTime = (texture->GetMTime() > texMTime ? texture->GetMTime() : texMTime);
      if (cellBO.ShaderSourceTime < texMTime)
      {
        return true;
      }
    }
  }

  return false;
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::UpdateShaders(
  vtkOpenGLHelper &cellBO, vtkRenderer* ren, vtkActor *actor)
{
  vtkOpenGLRenderWindow *renWin = vtkOpenGLRenderWindow::SafeDownCast(ren->GetRenderWindow());

  cellBO.VAO->Bind();
  this->LastBoundBO = &cellBO;

  // has something changed that would require us to recreate the shader?
  if (this->GetNeedToRebuildShaders(cellBO, ren, actor))
  {
    // build the shader source code
    std::map<vtkShader::Type,vtkShader *> shaders;
    vtkShader *vss = vtkShader::New();
    vss->SetType(vtkShader::Vertex);
    shaders[vtkShader::Vertex] = vss;
//    vtkShader *css = vtkShader::New();
//    css->SetType(vtkShader::TessControl);
//    shaders[vtkShader::TessControl] = css;
//    vtkShader *ess = vtkShader::New();
//    ess->SetType(vtkShader::TessEvaluation);
//    shaders[vtkShader::TessEvaluation] = ess;
    vtkShader *gss = vtkShader::New();
    gss->SetType(vtkShader::Geometry);
    shaders[vtkShader::Geometry] = gss;
    vtkShader *fss = vtkShader::New();
    fss->SetType(vtkShader::Fragment);
    shaders[vtkShader::Fragment] = fss;

    this->BuildShaders(shaders, ren, actor);

    // compile and bind the program if needed
    vtkShaderProgram *newShader =
      renWin->GetShaderCache()->ReadyShaderProgram(shaders);

    vss->Delete();
    fss->Delete();
    gss->Delete();
//    css->Delete();
//    ess->Delete();


    // if the shader changed reinitialize the VAO
    if (newShader != cellBO.Program)
    {
      cellBO.Program = newShader;
      // reset the VAO as the shader has changed
      cellBO.VAO->ReleaseGraphicsResources();
    }

    cellBO.ShaderSourceTime.Modified();
  }
  else
  {
    renWin->GetShaderCache()->ReadyShaderProgram(cellBO.Program);
  }

  if (cellBO.Program)
  {
    this->SetMapperShaderParameters(cellBO, ren, actor);
    this->SetPropertyShaderParameters(cellBO, ren, actor);
    this->SetCameraShaderParameters(cellBO, ren, actor);
    this->SetLightingShaderParameters(cellBO, ren, actor);

    // allow the program to set what it wants
    this->InvokeEvent(vtkCommand::UpdateShaderEvent, cellBO.Program);
  }

  vtkOpenGLCheckErrorMacro("failed after UpdateShader");
}

void NMVtkOpenGLPolyDataMapper::SetMapperShaderParameters(vtkOpenGLHelper &cellBO,
                                                      vtkRenderer* ren, vtkActor *actor)
{

  // Now to update the VAO too, if necessary.
  cellBO.Program->SetUniformi("PrimitiveIDOffset",
    this->PrimitiveIDOffset);

  if (cellBO.IBO->IndexCount &&
      (this->VBOs->GetMTime() > cellBO.AttributeUpdateTime ||
       cellBO.ShaderSourceTime > cellBO.AttributeUpdateTime))
  {
    cellBO.VAO->Bind();

    this->VBOs->AddAllAttributesToVAO(cellBO.Program, cellBO.VAO);

    if (!this->AppleBugPrimIDs.empty() &&
        cellBO.Program->IsAttributeUsed("appleBugPrimID"))
    {
      if (!cellBO.VAO->AddAttributeArray(cellBO.Program,
          this->AppleBugPrimIDBuffer,
          "appleBugPrimID",
           0, sizeof(float), VTK_UNSIGNED_CHAR, 4, true))
      {
        vtkErrorMacro(<< "Error setting 'appleBugPrimID' in shader VAO.");
      }
    }

    cellBO.AttributeUpdateTime.Modified();
  }

  if (this->HaveTextures(actor))
  {
    std::vector<texinfo> textures = this->GetTextures(actor);
    for (size_t i = 0; i < textures.size(); ++i)
    {
      vtkTexture *texture = textures[i].first;
      if (texture && cellBO.Program->IsUniformUsed(textures[i].second.c_str()))
      {
        int tunit = vtkOpenGLTexture::SafeDownCast(texture)->GetTextureUnit();
        cellBO.Program->SetUniformi(textures[i].second.c_str(), tunit);
      }
    }

    // check for tcoord transform matrix
    vtkInformation *info = actor->GetPropertyKeys();
    vtkOpenGLCheckErrorMacro("failed after Render");
    if (info && info->Has(vtkProp::GeneralTextureTransform()) &&
        cellBO.Program->IsUniformUsed("tcMatrix"))
    {
      double *dmatrix = info->Get(vtkProp::GeneralTextureTransform());
      float fmatrix[16];
      for (int i = 0; i < 4; i++)
      {
        for (int j = 0; j < 4; j++)
        {
          fmatrix[j*4+i] = dmatrix[i*4+j];
        }
      }
      cellBO.Program->SetUniformMatrix4x4("tcMatrix", fmatrix);
      vtkOpenGLCheckErrorMacro("failed after Render");
    }
  }

  if ((this->HaveCellScalars) &&
      cellBO.Program->IsUniformUsed("textureC"))
  {
    int tunit = this->CellScalarTexture->GetTextureUnit();
    cellBO.Program->SetUniformi("textureC", tunit);
  }

  if (this->HaveCellNormals && cellBO.Program->IsUniformUsed("textureN"))
  {
    int tunit = this->CellNormalTexture->GetTextureUnit();
    cellBO.Program->SetUniformi("textureN", tunit);
  }

  // Handle render pass setup:
  vtkInformation *info = actor->GetPropertyKeys();
  if (info && info->Has(vtkOpenGLRenderPass::RenderPasses()))
  {
    int numRenderPasses = info->Length(vtkOpenGLRenderPass::RenderPasses());
    for (int i = 0; i < numRenderPasses; ++i)
    {
      vtkObjectBase *rpBase = info->Get(vtkOpenGLRenderPass::RenderPasses(), i);
      vtkOpenGLRenderPass *rp = static_cast<vtkOpenGLRenderPass*>(rpBase);
      if (!rp->SetShaderParameters(cellBO.Program, this, actor, cellBO.VAO))
      {
        vtkErrorMacro("RenderPass::SetShaderParameters failed for renderpass: "
                      << rp->GetClassName());
      }
    }
  }

  vtkHardwareSelector* selector = ren->GetSelector();
  if (selector && cellBO.Program->IsUniformUsed("mapperIndex"))
  {
    cellBO.Program->SetUniform3f("mapperIndex", selector->GetPropColorValue());
  }

  if (this->GetNumberOfClippingPlanes() &&
      cellBO.Program->IsUniformUsed("numClipPlanes") &&
      cellBO.Program->IsUniformUsed("clipPlanes"))
  {
    // add all the clipping planes
    int numClipPlanes = this->GetNumberOfClippingPlanes();
    if (numClipPlanes > 6)
    {
      vtkErrorMacro(<< "OpenGL has a limit of 6 clipping planes");
      numClipPlanes = 6;
    }

    double shift[3] = {0.0, 0.0, 0.0};
    double scale[3] = {1.0, 1.0, 1.0};
    vtkOpenGLVertexBufferObject *vvbo = this->VBOs->GetVBO("vertexMC");
    if (vvbo && vvbo->GetCoordShiftAndScaleEnabled())
    {
      const std::vector<double> &vh = vvbo->GetShift();
      const std::vector<double> &vc = vvbo->GetScale();
      for (int i = 0; i < 3; ++i)
      {
        shift[i] = vh[i];
        scale[i] = vc[i];
      }
    }

    float planeEquations[6][4];
    for (int i = 0; i < numClipPlanes; i++)
    {
      double planeEquation[4];
      this->GetClippingPlaneInDataCoords(actor->GetMatrix(), i, planeEquation);

      // multiply by shift scale if set
      planeEquations[i][0] = planeEquation[0]/scale[0];
      planeEquations[i][1] = planeEquation[1]/scale[1];
      planeEquations[i][2] = planeEquation[2]/scale[2];
      planeEquations[i][3] = planeEquation[3]
        + planeEquation[0]*shift[0]
        + planeEquation[1]*shift[1]
        + planeEquation[2]*shift[2];
    }
    cellBO.Program->SetUniformi("numClipPlanes", numClipPlanes);
    cellBO.Program->SetUniform4fv("clipPlanes", 6, planeEquations);
  }

  // handle wide lines
  if (this->HaveWideLines(ren, actor) &&
      cellBO.Program->IsUniformUsed("lineWidthNVC"))
  {
      int vp[4];
      glGetIntegerv(GL_VIEWPORT, vp);
      float lineWidth[2];
      lineWidth[0] = 2.0*actor->GetProperty()->GetLineWidth()/vp[2];
      lineWidth[1] = 2.0*actor->GetProperty()->GetLineWidth()/vp[3];
      cellBO.Program->SetUniform2f("lineWidthNVC",lineWidth);
  }

  // handle coincident
  if (cellBO.Program->IsUniformUsed("coffset"))
  {
    float factor, offset;
    this->GetCoincidentParameters(ren, actor,factor,offset);
    cellBO.Program->SetUniformf("coffset",offset);
    // cfactor isn't always used when coffset is.
    if (cellBO.Program->IsUniformUsed("cfactor"))
    {
      cellBO.Program->SetUniformf("cfactor", factor);
    }
  }
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::SetLightingShaderParameters(
  vtkOpenGLHelper &cellBO,
  vtkRenderer* ren,
  vtkActor *)
{
  // for unlit there are no lighting parameters
  if (this->LastLightComplexity[&cellBO] < 1)
  {
    return;
  }

  vtkShaderProgram *program = cellBO.Program;
  vtkOpenGLRenderer *oren = static_cast<vtkOpenGLRenderer *>(ren);
  oren->UpdateLightingUniforms(program);
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::SetCameraShaderParameters(vtkOpenGLHelper &cellBO,
                                                    vtkRenderer* ren, vtkActor *actor)
{
  vtkShaderProgram *program = cellBO.Program;

  vtkOpenGLCamera *cam = (vtkOpenGLCamera *)(ren->GetActiveCamera());

  // [WMVD]C == {world, model, view, display} coordinates
  // E.g., WCDC == world to display coordinate transformation
  vtkMatrix4x4* wcdc;
  vtkMatrix4x4* wcvc;
  vtkMatrix3x3* norms;
  vtkMatrix4x4* vcdc;
  cam->GetKeyMatrices(ren, wcvc, norms, vcdc, wcdc);

  if (program->IsUniformUsed("ZCalcR"))
  {
    if (cam->GetParallelProjection())
    {
      program->SetUniformf("ZCalcS", vcdc->GetElement(2,2));
    }
    else
    {
      program->SetUniformf("ZCalcS", -0.5*vcdc->GetElement(2,2) + 0.5);
    }
    if (this->DrawingSpheres(cellBO, actor))
    {
      program->SetUniformf("ZCalcR",
        actor->GetProperty()->GetPointSize()/
          (ren->GetSize()[0] * vcdc->GetElement(0,0)));
    }
    else
    {
      program->SetUniformf("ZCalcR",
        actor->GetProperty()->GetLineWidth()/
          (ren->GetSize()[0] * vcdc->GetElement(0,0)));
    }
  }

  // If the VBO coordinates were shifted and scaled, apply the inverse transform
  // to the model->view matrix:
  vtkOpenGLVertexBufferObject *vvbo = this->VBOs->GetVBO("vertexMC");
  if (vvbo && vvbo->GetCoordShiftAndScaleEnabled())
  {
    if (!actor->GetIsIdentity())
    {
      vtkMatrix4x4* mcwc;
      vtkMatrix3x3* anorms;
      static_cast<vtkOpenGLActor *>(actor)->GetKeyMatrices(mcwc,anorms);
      vtkMatrix4x4::Multiply4x4(this->VBOShiftScale, mcwc, this->TempMatrix4);
      vtkMatrix4x4::Multiply4x4(this->TempMatrix4, wcdc, this->TempMatrix4);
      program->SetUniformMatrix("MCDCMatrix", this->TempMatrix4);
      if (program->IsUniformUsed("MCVCMatrix"))
      {
        vtkMatrix4x4::Multiply4x4(this->VBOShiftScale, mcwc, this->TempMatrix4);
        vtkMatrix4x4::Multiply4x4(this->TempMatrix4, wcvc, this->TempMatrix4);
        program->SetUniformMatrix("MCVCMatrix", this->TempMatrix4);
      }
      if (program->IsUniformUsed("normalMatrix"))
      {
        vtkMatrix3x3::Multiply3x3(anorms, norms, this->TempMatrix3);
        program->SetUniformMatrix("normalMatrix", this->TempMatrix3);
      }
    }
    else
    {
      vtkMatrix4x4::Multiply4x4(this->VBOShiftScale, wcdc, this->TempMatrix4);
      program->SetUniformMatrix("MCDCMatrix", this->TempMatrix4);
      if (program->IsUniformUsed("MCVCMatrix"))
      {
        vtkMatrix4x4::Multiply4x4(this->VBOShiftScale, wcvc, this->TempMatrix4);
        program->SetUniformMatrix("MCVCMatrix", this->TempMatrix4);
      }
      if (program->IsUniformUsed("normalMatrix"))
      {
        program->SetUniformMatrix("normalMatrix", norms);
      }
    }
  }
  else
  {
    if (!actor->GetIsIdentity())
    {
      vtkMatrix4x4 *mcwc;
      vtkMatrix3x3 *anorms;
      ((vtkOpenGLActor *)actor)->GetKeyMatrices(mcwc,anorms);
      vtkMatrix4x4::Multiply4x4(mcwc, wcdc, this->TempMatrix4);
      program->SetUniformMatrix("MCDCMatrix", this->TempMatrix4);
      if (program->IsUniformUsed("MCVCMatrix"))
      {
        vtkMatrix4x4::Multiply4x4(mcwc, wcvc, this->TempMatrix4);
        program->SetUniformMatrix("MCVCMatrix", this->TempMatrix4);
      }
      if (program->IsUniformUsed("normalMatrix"))
      {
        vtkMatrix3x3::Multiply3x3(anorms, norms, this->TempMatrix3);
        program->SetUniformMatrix("normalMatrix", this->TempMatrix3);
      }
    }
    else
    {
      program->SetUniformMatrix("MCDCMatrix", wcdc);
      if (program->IsUniformUsed("MCVCMatrix"))
      {
        program->SetUniformMatrix("MCVCMatrix", wcvc);
      }
      if (program->IsUniformUsed("normalMatrix"))
      {
        program->SetUniformMatrix("normalMatrix", norms);
      }
    }
  }

  if (program->IsUniformUsed("cameraParallel"))
  {
    program->SetUniformi("cameraParallel", cam->GetParallelProjection());
  }
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::SetPropertyShaderParameters(vtkOpenGLHelper &cellBO,
                                                       vtkRenderer*, vtkActor *actor)
{
  vtkShaderProgram *program = cellBO.Program;

  vtkProperty *ppty = actor->GetProperty();

  {
  // Query the property for some of the properties that can be applied.
  float opacity = static_cast<float>(ppty->GetOpacity());
  double *aColor = this->DrawingEdgesOrVertices ?
    ppty->GetEdgeColor() : ppty->GetAmbientColor();
  aColor = cellBO.PrimitiveType == PrimitiveVertices ?
    ppty->GetVertexColor() : aColor;
  double aIntensity = (this->DrawingEdgesOrVertices
    && !this->DrawingTubesOrSpheres(cellBO, actor))
    ? 1.0 : ppty->GetAmbient();

  double *dColor = this->DrawingEdgesOrVertices ?
    ppty->GetEdgeColor() : ppty->GetDiffuseColor();
  dColor = cellBO.PrimitiveType == PrimitiveVertices ?
    ppty->GetVertexColor() : dColor;
  double dIntensity = (this->DrawingEdgesOrVertices
    && !this->DrawingTubesOrSpheres(cellBO, actor))
    ? 0.0 : ppty->GetDiffuse();

  double *sColor = ppty->GetSpecularColor();
  double sIntensity = (this->DrawingEdgesOrVertices && !this->DrawingTubes(cellBO, actor))
    ? 0.0 : ppty->GetSpecular();
  double specularPower = ppty->GetSpecularPower();

  // these are always set
  program->SetUniformf("opacityUniform", opacity);
  program->SetUniformf("ambientIntensity", aIntensity);
  program->SetUniformf("diffuseIntensity", dIntensity);
  program->SetUniform3f("ambientColorUniform", aColor);
  program->SetUniform3f("diffuseColorUniform", dColor);

  // handle specular
  if (this->LastLightComplexity[&cellBO])
  {
    program->SetUniformf("specularIntensity", sIntensity);
    program->SetUniform3f("specularColorUniform", sColor);
    program->SetUniformf("specularPowerUniform", specularPower);
    }
  }

  // now set the backface properties if we have them
  if (program->IsUniformUsed("ambientIntensityBF"))
  {
    ppty = actor->GetBackfaceProperty();

    float opacity = static_cast<float>(ppty->GetOpacity());
    double *aColor = ppty->GetAmbientColor();
    double aIntensity = ppty->GetAmbient();  // ignoring renderer ambient
    double *dColor = ppty->GetDiffuseColor();
    double dIntensity = ppty->GetDiffuse();
    double *sColor = ppty->GetSpecularColor();
    double sIntensity = ppty->GetSpecular();
    double specularPower = ppty->GetSpecularPower();

    program->SetUniformf("ambientIntensityBF", aIntensity);
    program->SetUniformf("diffuseIntensityBF", dIntensity);
    program->SetUniformf("opacityUniformBF", opacity);
    program->SetUniform3f("ambientColorUniformBF", aColor);
    program->SetUniform3f("diffuseColorUniformBF", dColor);

    // handle specular
    if (this->LastLightComplexity[&cellBO])
    {
      program->SetUniformf("specularIntensityBF", sIntensity);
      program->SetUniform3f("specularColorUniformBF", sColor);
      program->SetUniformf("specularPowerUniformBF", specularPower);
    }
  }

}

namespace
{
// helper to get the state of picking
int getPickState(vtkRenderer *ren)
{
  vtkHardwareSelector* selector = ren->GetSelector();
  if (selector)
  {
    return selector->GetCurrentPass();
  }

  if (ren->GetRenderWindow()->GetIsPicking())
  {
    return vtkHardwareSelector::ACTOR_PASS;
  }

  return vtkHardwareSelector::MIN_KNOWN_PASS - 1;
}
}

void NMVtkOpenGLPolyDataMapper::GetCoincidentParameters(
  vtkRenderer* ren, vtkActor *actor,
  float &factor, float &offset)
{
  // 1. ResolveCoincidentTopology is On and non zero for this primitive
  // type
  factor = 0.0;
  offset = 0.0;
  int primType = this->LastBoundBO->PrimitiveType;
  if ( this->GetResolveCoincidentTopology() == VTK_RESOLVE_SHIFT_ZBUFFER &&
       (primType == PrimitiveTris || primType == PrimitiveTriStrips))
  {
    // do something rough is better than nothing
    double zRes = this->GetResolveCoincidentTopologyZShift(); // 0 is no shift 1 is big shift
    double f = zRes*4.0;
    offset = f;
  }

  vtkProperty *prop = actor->GetProperty();
  if ((this->GetResolveCoincidentTopology() == VTK_RESOLVE_POLYGON_OFFSET) ||
      (prop->GetEdgeVisibility() && prop->GetRepresentation() == VTK_SURFACE))
  {
    double f = 0.0;
    double u = 0.0;
    if (primType == PrimitivePoints ||
        prop->GetRepresentation() == VTK_POINTS)
    {
      this->GetCoincidentTopologyPointOffsetParameter(u);
    }
    else if (primType == PrimitiveLines ||
        prop->GetRepresentation() == VTK_WIREFRAME)
    {
      this->GetCoincidentTopologyLineOffsetParameters(f,u);
    }
    else if (primType == PrimitiveTris || primType == PrimitiveTriStrips)
    {
      this->GetCoincidentTopologyPolygonOffsetParameters(f,u);
    }
    if (primType == PrimitiveTrisEdges ||
        primType == PrimitiveTriStripsEdges)
    {
      this->GetCoincidentTopologyPolygonOffsetParameters(f,u);
      f /= 2;
      u /= 2;
    }
    factor = f;
    offset = u;
  }

  // hardware picking always offset due to saved zbuffer
  // This gets you above the saved surface depth buffer.
  vtkHardwareSelector* selector = ren->GetSelector();
  if (selector &&
      selector->GetFieldAssociation() == vtkDataObject::FIELD_ASSOCIATION_POINTS)
  {
    offset -= 2.0;
  }
}

void NMVtkOpenGLPolyDataMapper::UpdateMaximumPointCellIds(vtkRenderer* ren, vtkActor *actor)
{
  vtkHardwareSelector* selector = ren->GetSelector();

  // our maximum point id is the is the index of the max of
  // 1) the maximum used value in our points array
  // 2) the largest used value in a provided pointIdArray
  // To make this quicker we use the number of points for (1)
  // and the max range for (2)
  vtkIdType maxPointId =
    this->CurrentInput->GetPoints()->GetNumberOfPoints()-1;
  if (this->CurrentInput && this->CurrentInput->GetPointData())
  {
    vtkIdTypeArray* pointArrayId = this->PointIdArrayName ?
      vtkArrayDownCast<vtkIdTypeArray>(
        this->CurrentInput->GetPointData()->GetArray(this->PointIdArrayName)) : nullptr;
    if (pointArrayId)
    {
      maxPointId = maxPointId < pointArrayId->GetRange()[1]
        ? pointArrayId->GetRange()[1] : maxPointId;
    }
  }
  selector->UpdateMaximumPointId(maxPointId);

  bool pointPicking = false;
  if (selector &&
      selector->GetFieldAssociation() == vtkDataObject::FIELD_ASSOCIATION_POINTS)
  {
    pointPicking = true;
  }

  // the maximum number of cells in a draw call is the max of
  // 1) the sum of IBO size divided by the stride
  // 2) the max of any used call in a cellIdArray
  vtkIdType maxCellId = 0;
  int representation = actor->GetProperty()->GetRepresentation();
  for (int i = PrimitiveStart; i < PrimitiveTriStrips + 1; i++)
  {
    if (this->Primitives[i].IBO->IndexCount)
    {
      GLenum mode = this->GetOpenGLMode(representation, i);
      if (pointPicking)
      {
        mode = GL_POINTS;
      }
      unsigned int stride = (mode == GL_POINTS ? 1 : (mode == GL_LINES ? 2 : 3));
      vtkIdType strideMax = static_cast<vtkIdType>(this->Primitives[i].IBO->IndexCount/stride);
      maxCellId += strideMax;
    }
  }

  if (this->CurrentInput && this->CurrentInput->GetCellData())
  {
    vtkIdTypeArray *cellArrayId = this->CellIdArrayName ?
      vtkArrayDownCast<vtkIdTypeArray>(
        this->CurrentInput->GetCellData()->GetArray(this->CellIdArrayName)) : nullptr;
    if (cellArrayId)
    {
      maxCellId = maxCellId < cellArrayId->GetRange()[1]
        ? cellArrayId->GetRange()[1] : maxCellId;
    }
  }
  selector->UpdateMaximumCellId(maxCellId);
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::RenderPieceStart(vtkRenderer* ren, vtkActor *actor)
{
  // Set the PointSize and LineWidget
#if GL_ES_VERSION_3_0 != 1
  glPointSize(actor->GetProperty()->GetPointSize()); // not on ES2
#endif

  // timer calls take time, for lots of "small" actors
  // the timer can be a big hit. So we only update
  // once per million cells or every 100 renders
  // whichever happens first
  vtkIdType numCells = this->CurrentInput->GetNumberOfCells();
  if (numCells != 0)
  {
    this->TimerQueryCounter++;
    if (this->TimerQueryCounter > 100 ||
    static_cast<double>(this->TimerQueryCounter)
      > 1000000.0 / numCells)
    {
      this->TimerQuery->ReusableStart();
      this->TimerQueryCounter = 0;
    }
  }

  vtkHardwareSelector* selector = ren->GetSelector();
  int picking = getPickState(ren);
  if (this->LastSelectionState != picking)
  {
    this->SelectionStateChanged.Modified();
    this->LastSelectionState = picking;
  }

  this->PrimitiveIDOffset = 0;

  // make sure the BOs are up to date
  this->UpdateBufferObjects(ren, actor);

  // render points for point picking in a special way
  if (selector &&
      selector->GetFieldAssociation() == vtkDataObject::FIELD_ASSOCIATION_POINTS)
  {
    static_cast<vtkOpenGLRenderer *>(ren)->GetState()->vtkglDepthMask(GL_FALSE);
  }
  if (selector && this->PopulateSelectionSettings)
  {
    selector->BeginRenderProp();
    if (selector->GetCurrentPass() == vtkHardwareSelector::COMPOSITE_INDEX_PASS)
    {
      selector->RenderCompositeIndex(1);
    }

    this->UpdateMaximumPointCellIds(ren, actor);
  }

  if (this->HaveCellScalars)
  {
    this->CellScalarTexture->Activate();
  }
  if (this->HaveCellNormals)
  {
    this->CellNormalTexture->Activate();
  }

  // If we are coloring by texture, then load the texture map.
  // Use Map as indicator, because texture hangs around.
  if (this->ColorTextureMap)
  {
    this->InternalColorTexture->Load(ren);
  }

  this->LastBoundBO = nullptr;
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::RenderPieceDraw(vtkRenderer* ren, vtkActor *actor)
{
  int representation = actor->GetProperty()->GetRepresentation();

  // render points for point picking in a special way
  // all cell types should be rendered as points
  vtkHardwareSelector* selector = ren->GetSelector();
  bool pointPicking = false;
  if (selector &&
      selector->GetFieldAssociation() == vtkDataObject::FIELD_ASSOCIATION_POINTS)
  {
    pointPicking = true;
  }

  bool draw_surface_with_edges =
    (actor->GetProperty()->GetEdgeVisibility() && representation == VTK_SURFACE) && !selector;
  int numVerts = this->VBOs->GetNumberOfTuples("vertexMC");
  for (int i = PrimitiveStart;
       i < (draw_surface_with_edges ? PrimitiveEnd : PrimitiveTriStrips + 1); i++)
  {
    this->DrawingEdgesOrVertices = (i > PrimitiveTriStrips ? true : false);
    if (this->Primitives[i].IBO->IndexCount)
    {
      GLenum mode = this->GetOpenGLMode(representation, i);
      //mode = GL_PATCHES;
      if (pointPicking)
      {
  #if GL_ES_VERSION_3_0 != 1
        glPointSize(this->GetPointPickingPrimitiveSize(i));
  #endif
        mode = GL_POINTS;
      }

      // Update/build/etc the shader.
      this->UpdateShaders(this->Primitives[i], ren, actor);

      if (mode == GL_LINES && !this->HaveWideLines(ren,actor))
      {
        glLineWidth(actor->GetProperty()->GetLineWidth());
      }

//      GLint MaxPatchVertices = 0;
//      glGetIntegerv(GL_MAX_PATCH_VERTICES, &MaxPatchVertices);
//      std::stringstream dmsg;
//      dmsg << "Max supported patch vertices " << MaxPatchVertices << std::endl;
//      vtkDebugMacro(<< dmsg.str());
//      glPatchParameteri(GL_PATCH_VERTICES, 3);
//      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

      this->Primitives[i].IBO->Bind();
      glDrawRangeElements(mode, 0,
                          static_cast<GLuint>(numVerts - 1),
                          static_cast<GLsizei>(this->Primitives[i].IBO->IndexCount),
                          GL_UNSIGNED_INT,
                          nullptr);
      this->Primitives[i].IBO->Release();

      int stride = (mode == GL_POINTS ? 1 : (mode == GL_LINES ? 2 : 3));
      this->PrimitiveIDOffset += (int)this->Primitives[i].IBO->IndexCount/stride;
    }
  }
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::RenderPieceFinish(vtkRenderer* ren,
  vtkActor *)
{
  vtkHardwareSelector* selector = ren->GetSelector();
  // render points for point picking in a special way
  if (selector &&
      selector->GetFieldAssociation() == vtkDataObject::FIELD_ASSOCIATION_POINTS)
  {
    static_cast<vtkOpenGLRenderer *>(ren)->GetState()->vtkglDepthMask(GL_TRUE);
  }
  if (selector && this->PopulateSelectionSettings)
  {
    selector->EndRenderProp();
  }

  if (this->LastBoundBO)
  {
    this->LastBoundBO->VAO->Release();
  }

  if (this->ColorTextureMap)
  {
    this->InternalColorTexture->PostRender(ren);
  }

  // timer calls take time, for lots of "small" actors
  // the timer can be a big hit. So we assume zero time
  // for anything less than 100K cells
  if (this->TimerQueryCounter == 0)
  {
    this->TimerQuery->ReusableStop();
    this->TimeToDraw = this->TimerQuery->GetReusableElapsedSeconds();
    // If the timer is not accurate enough, set it to a small
    // time so that it is not zero
    if (this->TimeToDraw == 0.0)
    {
      this->TimeToDraw = 0.0001;
    }
  }

  if (this->HaveCellScalars)
  {
    this->CellScalarTexture->Deactivate();
  }
  if (this->HaveCellNormals)
  {
    this->CellNormalTexture->Deactivate();
  }

  this->UpdateProgress(1.0);
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::RenderPiece(vtkRenderer* ren, vtkActor *actor)
{
  // Make sure that we have been properly initialized.
  if (ren->GetRenderWindow()->CheckAbortStatus())
  {
    return;
  }

  this->ResourceCallback->RegisterGraphicsResources(
    static_cast<vtkOpenGLRenderWindow *>(ren->GetRenderWindow()));

  //this->CurrentInput = this->GetInput();

  if (this->GetInput() == nullptr)
  {
    vtkErrorMacro(<< "No input!");
    return;
  }

  this->InvokeEvent(vtkCommand::StartEvent,nullptr);
  if (!this->Static)
  {
    this->GetInputAlgorithm()->Update();
  }
  this->InvokeEvent(vtkCommand::EndEvent,nullptr);

  // if there are no points then we are done
  if (!this->GetInput()->GetPoints())
  {
    return;
  }

  if (   m_Tris.GetPointer() == nullptr
      || m_Tris->GetMTime() < this->GetInput()->GetMTime()
     )
  {
      vtkSmartPointer<NMPolygonToTriangles> tess = vtkSmartPointer<NMPolygonToTriangles>::New();
      tess->SetInputData(this->GetInput());
      //tess->SetInputColors(vtkLookupTable::SafeDownCast(this->GetLookupTable()));
      tess->Update();
      //this->LookupTable = vtkScalarsToColors::SafeDownCast(tess->GetOutputColors());
      this->m_Tris = tess->GetOutput();
      //this->m_OrigInput = this->CurrentInput;
      this->CurrentInput = m_Tris;
      this->TriIdsToPolyIds = tess->GetPolyIdMap();
  }

  if (this->LookupTable != nullptr && this->LastColorChange < this->LookupTable->GetMTime())
  {
      this->UpdateColorMapping();
  }

  this->UseLookupTableScalarRangeOn();

  this->RenderPieceStart(ren, actor);
  this->RenderPieceDraw(ren, actor);
  this->RenderPieceFinish(ren, actor);
}

void NMVtkOpenGLPolyDataMapper::UpdateColorMapping()
{
    if (this->TriIdsToPolyIds.size() == 0)
    {
        return;
    }

    vtkLookupTable* in = vtkLookupTable::SafeDownCast(this->LookupTable);
    if (in == nullptr)
    {
        return;
    }

    int nclrs = this->TriIdsToPolyIds.size();
    vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(nclrs);

    double inclr [] = {1,1,1,1};
    for (int i=0; i < nclrs; ++i)
    {
        in->GetTableValue(this->TriIdsToPolyIds[i], inclr);
        lut->SetTableValue(i, inclr);
    }

    this->SetLookupTable(vtkScalarsToColors::SafeDownCast(lut));
    this->LastColorChange = this->LookupTable->GetMTime();
}


void NMVtkOpenGLPolyDataMapper::PrintLookupTable(vtkLookupTable *lut, std::string label)
{
    int nclrs = lut->GetNumberOfTableValues();
    std::cout << label << "\n";
    for (int k=0; k < nclrs; ++k)
    {
        std::cout << "  " << k << "\t";
        double farbe[4];
        lut->GetTableValue(k, farbe);
        std::cout << std::setprecision(3)
                      << farbe[0] << " " << farbe[1] << " "
                      << farbe[2] << " " << farbe[3] << "\n";
        if (farbe[3] == 0)
            farbe[3] = 1.0;

        lut->SetTableValue(k, farbe);
    }
}

//-------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::ComputeBounds()
{
  if (!this->GetInput())
  {
    vtkMath::UninitializeBounds(this->Bounds);
    return;
  }
  this->GetInput()->GetBounds(this->Bounds);
}

//-------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::UpdateBufferObjects(vtkRenderer *ren, vtkActor *act)
{
  // Rebuild buffers if needed
  if (this->GetNeedToRebuildBufferObjects(ren,act))
  {
    this->BuildBufferObjects(ren,act);
  }
}

//-------------------------------------------------------------------------
bool NMVtkOpenGLPolyDataMapper::GetNeedToRebuildBufferObjects(
  vtkRenderer *vtkNotUsed(ren), vtkActor *act)
{
  // we use a state vector instead of just mtime because
  // we do not want to check the actor's mtime.  Actor
  // changes mtime every time it's position changes. But
  // changing an actor's position does not require us to
  // rebuild all the VBO/IBOs. So we only watch the mtime
  // of the property/texture. But if someone changes the
  // Property on an actor the mtime may actually go down
  // because the new property has an older mtime. So
  // we watch the actual mtime, to see if it changes as
  // opposed to just checking if it is greater.
  this->TempState.Clear();
  this->TempState.Append(act->GetProperty()->GetMTime(),"actor mtime");
  this->TempState.Append(
    this->CurrentInput ? this->CurrentInput->GetMTime() : 0, "input mtime");
  this->TempState.Append(
    act->GetTexture() ? act->GetTexture()->GetMTime() : 0, "texture mtime");

  if (this->VBOBuildState != this->TempState ||
      this->VBOBuildTime < this->GetMTime())
  {
    this->VBOBuildState = this->TempState;
    return true;
  }

  return false;
}

// create the cell scalar array adjusted for ogl Cells


void NMVtkOpenGLPolyDataMapper::AppendCellTextures(
  vtkRenderer * /*ren*/,
  vtkActor *,
  vtkCellArray *prims[4],
  int representation,
  std::vector<unsigned char> &newColors,
  std::vector<float> &newNorms,
  vtkPolyData *poly)
{
  vtkPoints *points = poly->GetPoints();

  if (this->HaveCellScalars || this->HaveCellNormals)
  {
    this->UpdateCellMaps(this->HaveAppleBug,
                          poly, prims, representation, points);

    if (this->HaveCellScalars)
    {
      int numComp = this->Colors->GetNumberOfComponents();
      unsigned char *colorPtr = this->Colors->GetPointer(0);
      assert(numComp == 4);
      newColors.reserve(numComp*this->CellCellMap.size());
      // use a single color value?
      if (this->FieldDataTupleId > -1 &&
          this->ScalarMode == VTK_SCALAR_MODE_USE_FIELD_DATA)
      {
        for (size_t i = 0; i < this->CellCellMap.size(); i++)
        {
          for (int j = 0; j < numComp; j++)
          {
            newColors.push_back(colorPtr[this->FieldDataTupleId*numComp + j]);
          }
        }
      }
      else
      {
        for (size_t i = 0; i < this->CellCellMap.size(); i++)
        {
          for (int j = 0; j < numComp; j++)
          {
            newColors.push_back(colorPtr[this->CellCellMap[i]*numComp + j]);
          }
        }
      }
    }

    if (this->HaveCellNormals)
    {
      // create the cell scalar array adjusted for ogl Cells
      vtkDataArray *n = this->CurrentInput->GetCellData()->GetNormals();
      newNorms.reserve(4*this->CellCellMap.size());
      for (size_t i = 0; i < this->CellCellMap.size(); i++)
      {
        // RGB32F requires a later version of OpenGL than 3.2
        // with 3.2 we know we have RGBA32F hence the extra value
        double *norms = n->GetTuple(this->CellCellMap[i]);
        newNorms.push_back(norms[0]);
        newNorms.push_back(norms[1]);
        newNorms.push_back(norms[2]);
        newNorms.push_back(0);
      }
    }
  }
}

void NMVtkOpenGLPolyDataMapper::BuildCellTextures(
  vtkRenderer *ren,
  vtkActor *actor,
  vtkCellArray *prims[4],
  int representation)
{
  // create the cell scalar array adjusted for ogl Cells
  std::vector<unsigned char> newColors;
  std::vector<float> newNorms;
  this->AppendCellTextures(ren, actor, prims, representation,
    newColors, newNorms, this->CurrentInput);

  // allocate as needed
  if (this->HaveCellScalars)
  {
    if (!this->CellScalarTexture)
    {
      this->CellScalarTexture = vtkTextureObject::New();
      this->CellScalarBuffer = vtkOpenGLBufferObject::New();
      this->CellScalarBuffer->SetType(vtkOpenGLBufferObject::TextureBuffer);
    }
    this->CellScalarTexture->SetContext(
      static_cast<vtkOpenGLRenderWindow*>(ren->GetVTKWindow()));
    this->CellScalarBuffer->Upload(newColors,
      vtkOpenGLBufferObject::TextureBuffer);
    this->CellScalarTexture->CreateTextureBuffer(
      static_cast<unsigned int>(newColors.size()/4),
      4,
      VTK_UNSIGNED_CHAR,
      this->CellScalarBuffer);
  }

  if (this->HaveCellNormals)
  {
    if (!this->CellNormalTexture)
    {
      this->CellNormalTexture = vtkTextureObject::New();
      this->CellNormalBuffer = vtkOpenGLBufferObject::New();
      this->CellNormalBuffer->SetType(vtkOpenGLBufferObject::TextureBuffer);
    }
    this->CellNormalTexture->SetContext(
      static_cast<vtkOpenGLRenderWindow*>(ren->GetVTKWindow()));

    // do we have float texture support ?
    int ftex =
      static_cast<vtkOpenGLRenderWindow *>(ren->GetRenderWindow())->
        GetDefaultTextureInternalFormat(VTK_FLOAT, 4, false, true, false);

    if (ftex)
    {
      this->CellNormalBuffer->Upload(newNorms,
        vtkOpenGLBufferObject::TextureBuffer);
      this->CellNormalTexture->CreateTextureBuffer(
        static_cast<unsigned int>(newNorms.size()/4),
        4, VTK_FLOAT,
        this->CellNormalBuffer);
    }
    else
    {
      // have to convert to unsigned char if no float support
      std::vector<unsigned char> ucNewNorms;
      ucNewNorms.resize(newNorms.size());
      for (size_t i = 0; i < newNorms.size(); i++)
      {
        ucNewNorms[i] = 127.0*(newNorms[i] + 1.0);
      }
      this->CellNormalBuffer->Upload(ucNewNorms,
        vtkOpenGLBufferObject::TextureBuffer);
      this->CellNormalTexture->CreateTextureBuffer(
        static_cast<unsigned int>(newNorms.size()/4),
        4, VTK_UNSIGNED_CHAR,
        this->CellNormalBuffer);
    }
  }
}

// on some apple systems gl_PrimitiveID does not work
// correctly.  So we have to make sure there are no
// shared vertices and build an array that maps verts
// to their cell id
vtkPolyData *NMVtkOpenGLPolyDataMapper::HandleAppleBug(
  vtkPolyData *poly,
  std::vector<float> &buffData
  )
{
  const vtkIdType* indices = nullptr;
  vtkIdType npts = 0;

  vtkPolyData *newPD = vtkPolyData::New();
  newPD->GetCellData()->PassData(poly->GetCellData());
  vtkPoints *points = poly->GetPoints();
  vtkPoints *newPoints = vtkPoints::New();
  newPD->SetPoints(newPoints);
  vtkPointData *pointData = poly->GetPointData();
  vtkPointData *newPointData = newPD->GetPointData();
  newPointData->CopyStructure(pointData);
  newPointData->CopyAllocate(pointData);

  vtkCellArray *prims[4];
  prims[0] =  poly->GetVerts();
  prims[1] =  poly->GetLines();
  prims[2] =  poly->GetPolys();
  prims[3] =  poly->GetStrips();

  // build a new PolyData with no shared cells

  // for each prim type
  vtkIdType newPointCount = 0;
  buffData.reserve(points->GetNumberOfPoints());
  for (int j = 0; j < 4; j++)
  {
    vtkIdType newCellCount = 0;
    if (prims[j]->GetNumberOfCells())
    {
      vtkCellArray *ca = vtkCellArray::New();
      switch (j)
      {
        case 0: newPD->SetVerts(ca); break;
        case 1: newPD->SetLines(ca); break;
        case 2: newPD->SetPolys(ca); break;
        case 3: newPD->SetStrips(ca); break;
      }

      // foreach cell
      for (prims[j]->InitTraversal(); prims[j]->GetNextCell(npts, indices); )
      {
        ca->InsertNextCell(npts);
        vtkFourByteUnion c;
        c.c[0] = newCellCount&0xff;
        c.c[1] = (newCellCount >> 8)&0xff;
        c.c[2] = (newCellCount >> 16)&0xff;
        c.c[3] =  0;
        for (vtkIdType i = 0; i < npts; ++i)
        {
          // insert point data
          newPoints->InsertNextPoint(points->GetPoint(indices[i]));
          ca->InsertCellPoint(newPointCount);
          newPointData->CopyData(pointData,indices[i],newPointCount);
          buffData.push_back(c.f);
          newPointCount++;
        }
        newCellCount++;
      }
      ca->Delete();
    }
  }

  newPoints->Delete();
  return newPD;
}

//-------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::BuildBufferObjects(vtkRenderer *ren, vtkActor *act)
{
  vtkPolyData *poly = this->CurrentInput;

  if (poly == nullptr)
  {
    return;
  }

  // For vertex coloring, this sets this->Colors as side effect.
  // For texture map coloring, this sets ColorCoordinates
  // and ColorTextureMap as a side effect.
  // I moved this out of the conditional because it is fast.
  // Color arrays are cached. If nothing has changed,
  // then the scalars do not have to be regenerted.

  // this messes our tris to poly mapping up, so use our own mapping here
  // but need to set Colors instead, which would have been done by the
  // as a side effect

  if (this->LookupTable == nullptr)
  {
    this->MapScalars(1.0);
  }
  else
  {
      vtkLookupTable* lut = vtkLookupTable::SafeDownCast(this->LookupTable);
      vtkUnsignedCharArray* colors = lut->GetTable();
      this->Colors = colors;
      this->Register(colors);
  }


  // If we are coloring by texture, then load the texture map.
  if (this->ColorTextureMap)
  {
    if (this->InternalColorTexture == nullptr)
    {
      this->InternalColorTexture = vtkOpenGLTexture::New();
      this->InternalColorTexture->RepeatOff();
    }
    this->InternalColorTexture->SetInputData(this->ColorTextureMap);
  }

  this->HaveCellScalars = false;
  vtkDataArray *c = this->Colors;
  if (this->ScalarVisibility)
  {
    // We must figure out how the scalars should be mapped to the polydata.
    if ( (this->ScalarMode == VTK_SCALAR_MODE_USE_CELL_DATA ||
          this->ScalarMode == VTK_SCALAR_MODE_USE_CELL_FIELD_DATA ||
          this->ScalarMode == VTK_SCALAR_MODE_USE_FIELD_DATA ||
          !poly->GetPointData()->GetScalars() )
         && this->ScalarMode != VTK_SCALAR_MODE_USE_POINT_FIELD_DATA
         && this->Colors)
    {
      this->HaveCellScalars = true;
      c = nullptr;
    }
  }

  this->HaveCellNormals = false;
  // Do we have cell normals?
  vtkDataArray *n =
    (act->GetProperty()->GetInterpolation() != VTK_FLAT) ? poly->GetPointData()->GetNormals() : nullptr;
  if (n == nullptr && poly->GetCellData()->GetNormals())
  {
    this->HaveCellNormals = true;
  }

  int representation = act->GetProperty()->GetRepresentation();

  // check if this system is subject to the apple/amd primID bug
  this->HaveAppleBug =
    static_cast<vtkOpenGLRenderer *>(ren)->HaveApplePrimitiveIdBug();
  if (this->HaveAppleBugForce == 1)
  {
    this->HaveAppleBug = false;
  }
  if (this->HaveAppleBugForce == 2)
  {
    this->HaveAppleBug = true;
  }

  vtkCellArray *prims[4];
  prims[0] =  poly->GetVerts();
  prims[1] =  poly->GetLines();
  prims[2] =  poly->GetPolys();
  prims[3] =  poly->GetStrips();

  // only rebuild what we need to
  // if the data or mapper or selection state changed
  // then rebuild the cell arrays
  this->TempState.Clear();
  this->TempState.Append(
    prims[0]->GetNumberOfCells() ? prims[0]->GetMTime() : 0, "prim0 mtime");
  this->TempState.Append(
    prims[1]->GetNumberOfCells() ? prims[1]->GetMTime() : 0, "prim1 mtime");
  this->TempState.Append(
    prims[2]->GetNumberOfCells() ? prims[2]->GetMTime() : 0, "prim2 mtime");
  this->TempState.Append(
    prims[3]->GetNumberOfCells() ? prims[3]->GetMTime() : 0, "prim3 mtime");
  this->TempState.Append(representation, "representation");
  this->TempState.Append(this->LastSelectionState, "last selection state");
  this->TempState.Append(poly->GetMTime(), "polydata mtime");
  this->TempState.Append(this->GetMTime(), "this mtime");
  if (this->CellTextureBuildState != this->TempState)
  {
    this->CellTextureBuildState = this->TempState;
    this->BuildCellTextures(ren, act, prims, representation);
  }

  // on Apple Macs with the AMD PrimID bug <rdar://20747550>
  // we use a slow painful approach to work around it (pre 10.11).
  this->AppleBugPrimIDs.resize(0);
  if (this->HaveAppleBug &&
      (this->HaveCellNormals || this->HaveCellScalars))
  {
    if (!this->AppleBugPrimIDBuffer)
    {
      this->AppleBugPrimIDBuffer = vtkOpenGLBufferObject::New();
    }
    poly = this->HandleAppleBug(poly, this->AppleBugPrimIDs);
    this->AppleBugPrimIDBuffer->Bind();
    this->AppleBugPrimIDBuffer->Upload(
     this->AppleBugPrimIDs, vtkOpenGLBufferObject::ArrayBuffer);
    this->AppleBugPrimIDBuffer->Release();

#ifndef NDEBUG
    static bool warnedAboutBrokenAppleDriver = false;
    if (!warnedAboutBrokenAppleDriver)
    {
      vtkWarningMacro("VTK is working around a bug in Apple-AMD hardware related to gl_PrimitiveID.  This may cause significant memory and performance impacts. Your hardware has been identified as vendor "
        << (const char *)glGetString(GL_VENDOR) << " with renderer of "
        << (const char *)glGetString(GL_RENDERER) << " and version "
        << (const char *)glGetString(GL_VERSION));
      warnedAboutBrokenAppleDriver = true;
    }
#endif
    if (n)
    {
      n = (act->GetProperty()->GetInterpolation() != VTK_FLAT) ?
            poly->GetPointData()->GetNormals() : nullptr;
    }
    if (c)
    {
      this->Colors->Delete();
      this->Colors = nullptr;
      this->MapScalars(poly,1.0);
      c = this->Colors;
    }
  }

  // Set the texture if we are going to use texture
  // for coloring with a point attribute.
  vtkDataArray *tcoords = nullptr;
  if (this->HaveTCoords(poly))
  {
    if (this->InterpolateScalarsBeforeMapping && this->ColorCoordinates)
    {
      tcoords = this->ColorCoordinates;
    }
    else
    {
      tcoords = poly->GetPointData()->GetTCoords();
    }
  }

  vtkOpenGLRenderWindow *renWin = vtkOpenGLRenderWindow::SafeDownCast(ren->GetRenderWindow());
  vtkOpenGLVertexBufferObjectCache *cache = renWin->GetVBOCache();

  // rebuild VBO if needed
  for (auto &itr : this->ExtraAttributes)
  {
    vtkDataArray *da = poly->GetPointData()->GetArray(itr.second.DataArrayName.c_str());
    this->VBOs->CacheDataArray(itr.first.c_str(), da, cache, VTK_FLOAT);
  }

  this->VBOs->CacheDataArray("vertexMC", poly->GetPoints()->GetData(), cache, VTK_FLOAT);
  vtkOpenGLVertexBufferObject *posVBO = this->VBOs->GetVBO("vertexMC");
  if (posVBO)
  {
    posVBO->SetCoordShiftAndScaleMethod(
      static_cast<vtkOpenGLVertexBufferObject::ShiftScaleMethod>(this->ShiftScaleMethod));
  }

  this->VBOs->CacheDataArray("normalMC", n, cache, VTK_FLOAT);
  this->VBOs->CacheDataArray("scalarColor", c, cache, VTK_UNSIGNED_CHAR);
  this->VBOs->CacheDataArray("tcoord", tcoords, cache, VTK_FLOAT);
  this->VBOs->BuildAllVBOs(cache);

  // get it again as it may have been freed
  posVBO = this->VBOs->GetVBO("vertexMC");
  if (posVBO && posVBO->GetCoordShiftAndScaleEnabled())
  {
    std::vector<double> shift = posVBO->GetShift();
    std::vector<double> scale = posVBO->GetScale();
    this->VBOInverseTransform->Identity();
    this->VBOInverseTransform->Translate(shift[0], shift[1], shift[2]);
    this->VBOInverseTransform->Scale(1.0/scale[0], 1.0/scale[1], 1.0/scale[2]);
    this->VBOInverseTransform->GetTranspose(this->VBOShiftScale);
  }

  // now create the IBOs
  this->BuildIBO(ren, act, poly);

  // free up polydata if allocated due to apple bug
  if (poly != this->CurrentInput)
  {
    poly->Delete();
  }

  vtkOpenGLCheckErrorMacro("failed after BuildBufferObjects");

  this->VBOBuildTime.Modified(); // need to call all the time or GetNeedToRebuild will always return true;
}

//-------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::BuildIBO(
  vtkRenderer * /* ren */,
  vtkActor *act,
  vtkPolyData *poly)
{
  vtkCellArray *prims[4];
  prims[0] =  poly->GetVerts();
  prims[1] =  poly->GetLines();
  prims[2] =  poly->GetPolys();
  prims[3] =  poly->GetStrips();
  int representation = act->GetProperty()->GetRepresentation();

  vtkDataArray *ef = poly->GetPointData()->GetAttribute(
                    vtkDataSetAttributes::EDGEFLAG);
  vtkProperty *prop = act->GetProperty();

  bool draw_surface_with_edges =
    (prop->GetEdgeVisibility() && prop->GetRepresentation() == VTK_SURFACE);

  // do we really need to rebuild the IBO? Since the operation is costly we
  // construst a string of values that impact the IBO and see if that string has
  // changed

  // So...polydata can return a dummy CellArray when there are no lines
  this->TempState.Append(
    prims[0]->GetNumberOfCells() ? prims[0]->GetMTime() : 0, "prim0 mtime");
  this->TempState.Append(
    prims[1]->GetNumberOfCells() ? prims[1]->GetMTime() : 0, "prim1 mtime");
  this->TempState.Append(
    prims[2]->GetNumberOfCells() ? prims[2]->GetMTime() : 0, "prim2 mtime");
  this->TempState.Append(
    prims[3]->GetNumberOfCells() ? prims[3]->GetMTime() : 0, "prim3 mtime");
  this->TempState.Append(representation, "representation");
  this->TempState.Append(ef ? ef->GetMTime() : 0, "edge flags mtime");
  this->TempState.Append(draw_surface_with_edges, "draw surface with edges");

  if (this->IBOBuildState != this->TempState)
  {
    this->IBOBuildState = this->TempState;
    this->Primitives[PrimitivePoints].IBO->CreatePointIndexBuffer(prims[0]);

    if (representation == VTK_POINTS)
    {
      this->Primitives[PrimitiveLines].IBO->CreatePointIndexBuffer(prims[1]);
      this->Primitives[PrimitiveTris].IBO->CreatePointIndexBuffer(prims[2]);
      this->Primitives[PrimitiveTriStrips].IBO->CreatePointIndexBuffer(prims[3]);
    }
    else // WIREFRAME OR SURFACE
    {
      this->Primitives[PrimitiveLines].IBO->CreateLineIndexBuffer(prims[1]);

      if (representation == VTK_WIREFRAME)
      {
        if (ef)
        {
          if (ef->GetNumberOfComponents() != 1)
          {
            vtkDebugMacro(<< "Currently only 1d edge flags are supported.");
            ef = nullptr;
          }
          if (!ef->IsA("vtkUnsignedCharArray"))
          {
            vtkDebugMacro(<< "Currently only unsigned char edge flags are supported.");
            ef = nullptr;
          }
        }
        if (ef)
        {
          this->Primitives[PrimitiveTris].IBO->CreateEdgeFlagIndexBuffer(prims[2], ef);
        }
        else
        {
          this->Primitives[PrimitiveTris].IBO->CreateTriangleLineIndexBuffer(prims[2]);
        }
        this->Primitives[PrimitiveTriStrips].IBO->CreateStripIndexBuffer(prims[3], true);
      }
      else // SURFACE
      {
        this->Primitives[PrimitiveTris].IBO->CreateTriangleIndexBuffer(prims[2], poly->GetPoints());
        this->Primitives[PrimitiveTriStrips].IBO->CreateStripIndexBuffer(prims[3], false);
      }
    }

    // when drawing edges also build the edge IBOs
    if (draw_surface_with_edges)
    {
      if (ef)
      {
        if (ef->GetNumberOfComponents() != 1)
        {
          vtkDebugMacro(<< "Currently only 1d edge flags are supported.");
          ef = nullptr;
        }
        else if (!ef->IsA("vtkUnsignedCharArray"))
        {
          vtkDebugMacro(<< "Currently only unsigned char edge flags are supported.");
          ef = nullptr;
        }
      }
      if (ef)
      {
        this->Primitives[PrimitiveTrisEdges].IBO->CreateEdgeFlagIndexBuffer(prims[2], ef);
      }
      else
      {
        this->Primitives[PrimitiveTrisEdges].IBO->CreateTriangleLineIndexBuffer(prims[2]);
      }
      this->Primitives[PrimitiveTriStripsEdges].IBO->CreateStripIndexBuffer(prims[3], true);
    }

    if (prop->GetVertexVisibility())
    {
      // for all 4 types of primitives add their verts into the IBO
      this->Primitives[PrimitiveVertices].IBO->CreateVertexIndexBuffer(prims);
    }
  }
}

//-----------------------------------------------------------------------------

bool NMVtkOpenGLPolyDataMapper::GetIsOpaque()
{
  if (this->ScalarVisibility &&
      (this->ColorMode == VTK_COLOR_MODE_DEFAULT ||
       this->ColorMode == VTK_COLOR_MODE_DIRECT_SCALARS))
  {
    vtkPolyData* input =
      vtkPolyData::SafeDownCast(this->GetInputDataObject(0, 0));
    if (input)
    {
      int cellFlag;
      vtkDataArray* scalars = this->GetScalars(input,
        this->ScalarMode, this->ArrayAccessMode, this->ArrayId,
        this->ArrayName, cellFlag);
      if (scalars &&
          (scalars->IsA("vtkUnsignedCharArray") ||
           this->ColorMode == VTK_COLOR_MODE_DIRECT_SCALARS) &&
        (scalars->GetNumberOfComponents() ==  4 /*(RGBA)*/ ||
         scalars->GetNumberOfComponents() == 2 /*(LuminanceAlpha)*/))
      {
        int opacityIndex = scalars->GetNumberOfComponents() - 1;
        unsigned char opacity = 0;
        switch (scalars->GetDataType())
        {
          vtkTemplateMacro(
            vtkScalarsToColors::ColorToUChar(
              static_cast<VTK_TT>(scalars->GetRange(opacityIndex)[0]),
              &opacity));
        }
        if (opacity < 255)
        {
          // If the opacity is 255, despite the fact that the user specified
          // RGBA, we know that the Alpha is 100% opaque. So treat as opaque.
          return false;
        }
      }
    }
  }
  return this->Superclass::GetIsOpaque();
}


//----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::ShallowCopy(vtkAbstractMapper *mapper)
{
  NMVtkOpenGLPolyDataMapper *m = NMVtkOpenGLPolyDataMapper::SafeDownCast(mapper);
  if (m != nullptr)
  {
    this->SetPointIdArrayName(m->GetPointIdArrayName());
    this->SetCompositeIdArrayName(m->GetCompositeIdArrayName());
    this->SetProcessIdArrayName(m->GetProcessIdArrayName());
    this->SetCellIdArrayName(m->GetCellIdArrayName());
    this->SetVertexShaderCode(m->GetVertexShaderCode());
    this->SetGeometryShaderCode(m->GetGeometryShaderCode());
    this->SetFragmentShaderCode(m->GetFragmentShaderCode());
//    this->SetTessControlShaderCode(m->GetTessControlShaderCode());
//    this->SetTessEvaluationShaderCode(m->GetTessEvaluationShaderCode());
  }

  // Now do superclass
  this->vtkPolyDataMapper::ShallowCopy(mapper);
}

void NMVtkOpenGLPolyDataMapper::SetVBOShiftScaleMethod(int m)
{
  this->ShiftScaleMethod = m;
}

int NMVtkOpenGLPolyDataMapper::GetOpenGLMode(
  int representation,
  int primType)
{
  if (representation == VTK_POINTS ||
      primType == PrimitivePoints ||
      primType == PrimitiveVertices)
  {
    return GL_POINTS;
  }
  if (representation == VTK_WIREFRAME ||
      primType == PrimitiveLines ||
      primType == PrimitiveTrisEdges ||
      primType == PrimitiveTriStripsEdges)
  {
    return GL_LINES;
  }
  return GL_TRIANGLES;
}

int NMVtkOpenGLPolyDataMapper::GetPointPickingPrimitiveSize(int primType)
{
  if (primType == PrimitivePoints)
  {
    return 2;
  }
  if (primType == PrimitiveLines)
  {
    return 4;
  }
  return 6;
}

//----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::MapDataArrayToVertexAttribute(
    const char* vertexAttributeName,
    const char* dataArrayName,
    int fieldAssociation,
    int componentno
    )
{
  this->MapDataArray(vertexAttributeName,
    dataArrayName, "", fieldAssociation, componentno);
}

void NMVtkOpenGLPolyDataMapper::MapDataArrayToMultiTextureAttribute(
    const char *tname,
    const char* dataArrayName,
    int fieldAssociation,
    int componentno
    )
{
  std::string coordname = tname;
  coordname += "_coord";
  this->MapDataArray(coordname.c_str(),
    dataArrayName, tname, fieldAssociation, componentno);
}

void NMVtkOpenGLPolyDataMapper::MapDataArray(
    const char* vertexAttributeName,
    const char* dataArrayName,
    const char *tname,
    int fieldAssociation,
    int componentno
    )
{
 if (!vertexAttributeName)
  {
    return;
  }

  // store the mapping in the map
  this->RemoveVertexAttributeMapping(vertexAttributeName);
  if (!dataArrayName)
  {
    return;
  }

  NMVtkOpenGLPolyDataMapper::ExtraAttributeValue aval;
  aval.DataArrayName = dataArrayName;
  aval.FieldAssociation = fieldAssociation;
  aval.ComponentNumber = componentno;
  aval.TextureName = tname;

  this->ExtraAttributes.insert(
    std::make_pair(vertexAttributeName, aval));

  this->Modified();
}

//----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::RemoveVertexAttributeMapping(
  const char* vertexAttributeName)
{
  auto itr = this->ExtraAttributes.find(vertexAttributeName);
  if (itr != this->ExtraAttributes.end())
  {
    this->VBOs->RemoveAttribute(vertexAttributeName);
    this->ExtraAttributes.erase(itr);
    this->Modified();
  }
}

//----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::RemoveAllVertexAttributeMappings()
{
  for (auto itr = this->ExtraAttributes.begin();
    itr != this->ExtraAttributes.end();
    itr = this->ExtraAttributes.begin())
  {
    this->RemoveVertexAttributeMapping(itr->first.c_str());
  }
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
void NMVtkOpenGLPolyDataMapper::MakeCellCellMap(
  std::vector<vtkIdType> &cellCellMap,
  bool HaveAppleBug,
  vtkPolyData *poly,
  vtkCellArray **prims, int representation, vtkPoints *points)
{
  cellCellMap.clear();

  if (HaveAppleBug)
  {
    vtkIdType numCells = poly->GetNumberOfCells();
    for (vtkIdType i = 0; i < numCells; ++i)
    {
      cellCellMap.push_back(i);
    }
  }
  else
  {
    vtkOpenGLIndexBufferObject::CreateCellSupportArrays(
        prims, cellCellMap, representation, points);
  }
}

void NMVtkOpenGLPolyDataMapper::UpdateCellMaps(
  bool haveAppleBug,
  vtkPolyData *poly,
  vtkCellArray **prims, int representation, vtkPoints *points)
{
  std::ostringstream toString;
  toString.str("");
  toString.clear();
  toString << (prims[0]->GetNumberOfCells() ? prims[0]->GetMTime() : 0) <<
    'A' << (prims[1]->GetNumberOfCells() ? prims[1]->GetMTime() : 0) <<
    'B' << (prims[2]->GetNumberOfCells() ? prims[2]->GetMTime() : 0) <<
    'C' << (prims[3]->GetNumberOfCells() ? prims[3]->GetMTime() : 0) <<
    'D' << representation <<
    'E' << (points ? points->GetMTime() : 0);

  // is it up to date? If so return
  if (this->CellMapsBuildString == toString.str())
  {
    return;
  }

  this->MakeCellCellMap(this->CellCellMap,
    haveAppleBug, poly, prims, representation, points);

  this->CellMapsBuildString = toString.str();
}

namespace
{
  unsigned int convertToCells(
    unsigned int *offset,
    unsigned int *stride,
    unsigned int inval )
  {
    if (inval < offset[0])
    {
      return inval;
    }
    if (inval < offset[1])
    {
      return offset[0] + (inval - offset[0]) / stride[0];
    }
    return offset[0] + (offset[1] - offset[0]) / stride[0] + (inval - offset[1]) / stride[1];
  }
}

void NMVtkOpenGLPolyDataMapper::ProcessSelectorPixelBuffers(
  vtkHardwareSelector *sel,
  std::vector<unsigned int> &pixeloffsets,
  vtkProp *prop)
{
  vtkPolyData *poly = this->CurrentInput;

  if (!this->PopulateSelectionSettings || !poly)
  {
    return;
  }

  // which pass are we processing ?
  int currPass = sel->GetCurrentPass();

  // get some common useful values
  bool pointPicking =
    sel->GetFieldAssociation() == vtkDataObject::FIELD_ASSOCIATION_POINTS;
  vtkPointData *pd = poly->GetPointData();
  vtkCellData *cd = poly->GetCellData();
  unsigned char *rawplowdata = sel->GetRawPixelBuffer(vtkHardwareSelector::POINT_ID_LOW24);
  unsigned char *rawphighdata = sel->GetRawPixelBuffer(vtkHardwareSelector::POINT_ID_HIGH24);

  // handle process pass
  if (currPass == vtkHardwareSelector::PROCESS_PASS)
  {
    vtkUnsignedIntArray* processArray = nullptr;

    // point data is used for process_pass which seems odd
    if (sel->GetUseProcessIdFromData())
    {
      processArray = this->ProcessIdArrayName ?
        vtkArrayDownCast<vtkUnsignedIntArray>(
          pd->GetArray(this->ProcessIdArrayName)) : nullptr;
    }

    // do we need to do anything to the process pass data?
    unsigned char *processdata = sel->GetRawPixelBuffer(vtkHardwareSelector::PROCESS_PASS);
    if (processArray && processdata && rawplowdata)
    {
      // get the buffer pointers we need
      for (auto pos : pixeloffsets)
      {
        unsigned int inval = 0;
        if (rawphighdata)
        {
          inval = rawphighdata[pos];
          inval = inval << 8;
        }
        inval |= rawplowdata[pos + 2];
        inval = inval << 8;
        inval |= rawplowdata[pos + 1];
        inval = inval << 8;
        inval |= rawplowdata[pos];
        inval -= 1;
        unsigned int outval =  processArray->GetValue(inval) + 1;
        processdata[pos] = outval & 0xff;
        processdata[pos + 1] = (outval & 0xff00) >> 8;
        processdata[pos + 2] = (outval & 0xff0000) >> 16;
      }
    }
  }

  if (currPass == vtkHardwareSelector::POINT_ID_LOW24)
  {
    vtkIdTypeArray* pointArrayId = this->PointIdArrayName ?
      vtkArrayDownCast<vtkIdTypeArray>(
        pd->GetArray(this->PointIdArrayName)) : nullptr;

    // do we need to do anything to the point id data?
    if (rawplowdata && pointArrayId)
    {
      unsigned char *plowdata = sel->GetPixelBuffer(vtkHardwareSelector::POINT_ID_LOW24);

      for (auto pos : pixeloffsets)
      {
        unsigned int inval = 0;
        if (rawphighdata)
        {
          inval = rawphighdata[pos];
          inval = inval << 8;
        }
        inval |= rawplowdata[pos + 2];
        inval = inval << 8;
        inval |= rawplowdata[pos + 1];
        inval = inval << 8;
        inval |= rawplowdata[pos];
        inval -= 1;
        vtkIdType outval = pointArrayId->GetValue(inval) + 1;
        plowdata[pos] = outval & 0xff;
        plowdata[pos + 1] = (outval & 0xff00) >> 8;
        plowdata[pos + 2] = (outval & 0xff0000) >> 16;
      }
    }
  }

  if (currPass == vtkHardwareSelector::POINT_ID_HIGH24)
  {
    vtkIdTypeArray *pointArrayId = this->PointIdArrayName ?
      vtkArrayDownCast<vtkIdTypeArray>(
        pd->GetArray(this->PointIdArrayName)) : nullptr;

    // do we need to do anything to the point id data?
    if (rawphighdata && pointArrayId)
    {
      unsigned char *phighdata = sel->GetPixelBuffer(vtkHardwareSelector::POINT_ID_HIGH24);

      for (auto pos : pixeloffsets)
      {
        unsigned int inval = 0;
        inval = rawphighdata[pos];
        inval = inval << 8;
        inval |= rawplowdata[pos + 2];
        inval = inval << 8;
        inval |= rawplowdata[pos + 1];
        inval = inval << 8;
        inval |= rawplowdata[pos];
        inval -= 1;
        vtkIdType outval = pointArrayId->GetValue(inval) + 1;
        phighdata[pos] = (outval & 0xff000000) >> 24;
        phighdata[pos + 1] = (outval & 0xff00000000) >> 32;
        phighdata[pos + 2] = (outval & 0xff0000000000) >> 40;
      }
    }
  }

  // vars for cell based indexing
  vtkCellArray *prims[4];
  prims[0] =  poly->GetVerts();
  prims[1] =  poly->GetLines();
  prims[2] =  poly->GetPolys();
  prims[3] =  poly->GetStrips();

  int representation =
    static_cast<vtkActor *>(prop)->GetProperty()->GetRepresentation();

  unsigned char *rawclowdata = sel->GetRawPixelBuffer(vtkHardwareSelector::CELL_ID_LOW24);
  unsigned char *rawchighdata = sel->GetRawPixelBuffer(vtkHardwareSelector::CELL_ID_HIGH24);

  // build the mapping of point primID to cell primID
  // aka when we render triangles in point picking mode
  // how do we map primid to what would normally be primid
  unsigned int offset[2];
  unsigned int stride[2];
  offset[0] = static_cast<unsigned int>(this->Primitives[PrimitiveVertices].IBO->IndexCount);
  stride[0] = representation == VTK_POINTS ? 1 : 2;
  offset[1] = offset[0] + static_cast<unsigned int>(
    this->Primitives[PrimitiveLines].IBO->IndexCount);
  stride[1] = representation == VTK_POINTS ? 1 : representation == VTK_WIREFRAME ? 2 : 3;

  // do we need to do anything to the composite pass data?
  if (currPass == vtkHardwareSelector::COMPOSITE_INDEX_PASS)
  {
    unsigned char *compositedata =
      sel->GetPixelBuffer(vtkHardwareSelector::COMPOSITE_INDEX_PASS);

    vtkUnsignedIntArray *compositeArray = this->CompositeIdArrayName ?
      vtkArrayDownCast<vtkUnsignedIntArray>(
        cd->GetArray(this->CompositeIdArrayName)) : nullptr;

    if (compositedata && compositeArray && rawclowdata)
    {
      this->UpdateCellMaps(this->HaveAppleBug,
        poly,
        prims,
        representation,
        poly->GetPoints());

      for (auto pos : pixeloffsets)
      {
        unsigned int inval = 0;
        if (rawchighdata)
        {
          inval = rawchighdata[pos];
          inval = inval << 8;
        }
        inval |= rawclowdata[pos + 2];
        inval = inval << 8;
        inval |= rawclowdata[pos + 1];
        inval = inval << 8;
        inval |= rawclowdata[pos];
        inval -= 1;
        if (pointPicking)
        {
          inval = convertToCells(offset, stride, inval);
        }
        vtkIdType vtkCellId = this->CellCellMap[inval];
        unsigned int outval =  compositeArray->GetValue(vtkCellId) + 1;
        compositedata[pos] = outval & 0xff;
        compositedata[pos + 1] = (outval & 0xff00) >> 8;
        compositedata[pos + 2] = (outval & 0xff0000) >> 16;
      }
    }
  }

  // process the cellid array?
  if (currPass == vtkHardwareSelector::CELL_ID_LOW24)
  {
    vtkIdTypeArray *cellArrayId = this->CellIdArrayName ?
      vtkArrayDownCast<vtkIdTypeArray>(
        cd->GetArray(this->CellIdArrayName)) : nullptr;
    unsigned char *clowdata = sel->GetPixelBuffer(vtkHardwareSelector::CELL_ID_LOW24);

    if (rawclowdata)
    {
      this->UpdateCellMaps(this->HaveAppleBug,
        poly,
        prims,
        representation,
        poly->GetPoints());

      for (auto pos : pixeloffsets)
      {
        unsigned int inval = 0;
        if (rawchighdata)
        {
          inval = rawchighdata[pos];
          inval = inval << 8;
        }
        inval |= rawclowdata[pos + 2];
        inval = inval << 8;
        inval |= rawclowdata[pos + 1];
        inval = inval << 8;
        inval |= rawclowdata[pos];
        inval -= 1;
        if (pointPicking)
        {
          inval = convertToCells(offset, stride, inval);
        }
        vtkIdType outval = inval;
        if (this->CellCellMap.size())
        {
          outval = this->CellCellMap[outval];
        }
        if (cellArrayId)
        {
          outval = cellArrayId->GetValue(outval);
        }
        outval++;
        clowdata[pos] = outval & 0xff;
        clowdata[pos + 1] = (outval & 0xff00) >> 8;
        clowdata[pos + 2] = (outval & 0xff0000) >> 16;
      }
    }
  }

  if (currPass == vtkHardwareSelector::CELL_ID_HIGH24)
  {
    vtkIdTypeArray *cellArrayId = this->CellIdArrayName ?
      vtkArrayDownCast<vtkIdTypeArray>(
        cd->GetArray(this->CellIdArrayName)) : nullptr;
    unsigned char *chighdata = sel->GetPixelBuffer(vtkHardwareSelector::CELL_ID_HIGH24);

    if (rawchighdata)
    {
      this->UpdateCellMaps(this->HaveAppleBug,
        poly,
        prims,
        representation,
        poly->GetPoints());

      for (auto pos : pixeloffsets)
      {
        unsigned int inval = 0;
        inval = rawchighdata[pos];
        inval = inval << 8;
        inval |= rawclowdata[pos + 2];
        inval = inval << 8;
        inval |= rawclowdata[pos + 1];
        inval = inval << 8;
        inval |= rawclowdata[pos];
        inval -= 1;
        if (pointPicking)
        {
          inval = convertToCells(offset, stride, inval);
        }
        vtkIdType outval = inval;
        if (this->CellCellMap.size())
        {
          outval = this->CellCellMap[outval];
        }
        if (cellArrayId)
        {
          outval = cellArrayId->GetValue(outval);
        }
        outval++;
        chighdata[pos] = (outval & 0xff000000) >> 24;
        chighdata[pos + 1] = (outval & 0xff00000000) >> 32;
        chighdata[pos + 2] = (outval & 0xff0000000000) >> 40;
      }
    }
  }

}
