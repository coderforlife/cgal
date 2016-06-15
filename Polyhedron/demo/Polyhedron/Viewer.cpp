#include "Viewer.h"
#include <CGAL/gl.h>
#include <CGAL/Three/Scene_draw_interface.h>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGLViewer/manipulatedCameraFrame.h>
#include <QDebug>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <cmath>
#include <QApplication>


class Viewer_impl {
public:
  CGAL::Three::Scene_draw_interface* scene;
  bool antialiasing;
  bool twosides;
  bool macro_mode;
  bool inFastDrawing;
  bool inDrawWithNames;
  QPainter *painter;
  void draw_aux(bool with_names, Viewer*);
  //! Contains all the programs for the item rendering.
  mutable std::vector<QOpenGLShaderProgram*> shader_programs;
};
Viewer::Viewer(QWidget* parent, bool antialiasing)
  : CGAL::Three::Viewer_interface(parent)
{
  d = new Viewer_impl;
  d->scene = 0;
  d->antialiasing = antialiasing;
  d->twosides = false;
  d->macro_mode = false;
  d->inFastDrawing = true;
  d->inDrawWithNames = false;
  d->shader_programs.resize(NB_OF_PROGRAMS);
  textRenderer = new TextRenderer();
  connect( textRenderer, SIGNAL(sendMessage(QString,int)),
           this, SLOT(printMessage(QString,int)) );
  connect(&messageTimer, SIGNAL(timeout()), SLOT(hideMessage()));
  setShortcut(EXIT_VIEWER, 0);
  setShortcut(DRAW_AXIS, 0);
  setKeyDescription(Qt::Key_T,
                    tr("Turn the camera by 180 degrees"));
  setKeyDescription(Qt::Key_M,
                    tr("Toggle macro mode: useful to view details very near from the camera, "
                       "but decrease the z-buffer precision"));
  setKeyDescription(Qt::Key_A,
                      tr("Toggle the axis system visibility."));
  setKeyDescription(Qt::Key_I + Qt::CTRL,
                      tr("Toggle the primitive IDs visibility of the selected Item."));
  setKeyDescription(Qt::Key_D,
                      tr("Disable the distance between two points  visibility."));
#if QGLVIEWER_VERSION >= 0x020501
  //modify mouse bindings that have been updated
  setMouseBinding(Qt::Key(0), Qt::NoModifier, Qt::LeftButton, RAP_FROM_PIXEL, true, Qt::RightButton);
  setMouseBindingDescription(Qt::ShiftModifier, Qt::RightButton,
                             tr("Select and pop context menu"));
  setMouseBinding(Qt::Key_R, Qt::NoModifier, Qt::LeftButton, RAP_FROM_PIXEL);
  //use the new API for these
  setMouseBinding(Qt::ShiftModifier, Qt::LeftButton, SELECT);

  setMouseBindingDescription(Qt::Key(0), Qt::ShiftModifier, Qt::LeftButton,
                             tr("Selects and display context "
                                "menu of the selected item"));
  setMouseBindingDescription(Qt::Key_I, Qt::NoModifier, Qt::LeftButton,
                             tr("Show/hide the primitive ID."));
  setMouseBindingDescription(Qt::Key_D, Qt::NoModifier, Qt::LeftButton,
                             tr("Selects a point. When the second point is selected,  "
                                "displays the two points and the distance between them."));
#else
  setMouseBinding(Qt::SHIFT + Qt::LeftButton, SELECT);
  setMouseBindingDescription(Qt::SHIFT + Qt::RightButton,
                             tr("Selects and display context "
                                "menu of the selected item"));

#endif // QGLVIEWER_VERSION >= 2.5.0
  prev_radius = sceneRadius();
  axis_are_displayed = true;
  has_text = false;
  i_is_pressed = false;
  fpsTime.start();
  fpsCounter=0;
  f_p_s=0.0;
  fpsString=tr("%1Hz", "Frames per seconds, in Hertz").arg("?");
  distance_is_displayed = false;
  is_d_pressed = false;
}

Viewer::~Viewer()
{
  delete d;
}

void Viewer::setScene(CGAL::Three::Scene_draw_interface* scene)
{
  d->scene = scene;
}

bool Viewer::antiAliasing() const
{
  return d->antialiasing; 
}

void Viewer::setAntiAliasing(bool b)
{
  d->antialiasing = b;
  update();
}

void Viewer::setTwoSides(bool b)
{
  d->twosides = b;
  update();
}


void Viewer::setFastDrawing(bool b)
{
  d->inFastDrawing = b;
  update();
}

bool Viewer::inFastDrawing() const
{
  return (d->inFastDrawing
          && (camera()->frame()->isSpinning()
              || camera()->frame()->isManipulated()));
}

void Viewer::draw()
{ 
  makeCurrent();
  glEnable(GL_DEPTH_TEST);
  d->draw_aux(false, this);
}

void Viewer::fastDraw()
{
  d->draw_aux(false, this);
}

void Viewer::initializeGL()
{
  QGLViewer::initializeGL();
  initializeOpenGLFunctions();
  glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDARBPROC)this->context()->getProcAddress("glDrawArraysInstancedARB");
  if(!glDrawArraysInstanced)
  {
      qDebug()<<"glDrawArraysInstancedARB : extension not found. Spheres will be displayed as points.";
      extension_is_found = false;
  }
  else
      extension_is_found = true;

  glVertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORARBPROC)this->context()->getProcAddress("glVertexAttribDivisorARB");
  if(!glDrawArraysInstanced)
  {
      qDebug()<<"glVertexAttribDivisorARB : extension not found. Spheres will be displayed as points.";
      extension_is_found = false;
  }
  else
      extension_is_found = true;


  setBackgroundColor(::Qt::white);
  vao[0].create();
  for(int i=0; i<3; i++)
    buffers[i].create();

  //Vertex source code
  const char vertex_source[] =
  {
      "#version 120 \n"
      "attribute highp vec4 vertex;\n"
      "attribute highp vec3 normal;\n"
      "attribute highp vec4 colors;\n"
      "uniform highp mat4 mvp_matrix;\n"
      "uniform highp mat4 ortho_mat;\n"
      "uniform highp mat4 mv_matrix; \n"
      "uniform highp float width; \n"
      "uniform highp float height; \n"
      "varying highp vec4 fP; \n"
      "varying highp vec3 fN; \n"
      "varying highp vec4 color; \n"
      "void main(void)\n"
      "{\n"
      "   color = colors; \n"
      "   fP = mv_matrix * vertex; \n"
      "   fN = mat3(mv_matrix)* normal; \n"
      "   vec4 temp = vec4(mvp_matrix * vertex); \n"
      "   vec4 ort = ortho_mat * vec4(width-150, height-150, 0,0); \n"
      "   float ratio = width/height; \n"
      "   gl_Position =  ort +vec4(temp.x, temp.y, temp.z, 1.0); \n"
      "} \n"
      "\n"
  };
  //Fragment source code
  const char fragment_source[] =
  {
      "#version 120 \n"
      "varying highp vec4 color; \n"
      "varying highp vec4 fP; \n"
      "varying highp vec3 fN; \n"
      "uniform highp vec4 light_pos;  \n"
      "uniform highp vec4 light_diff; \n"
      "uniform highp vec4 light_spec; \n"
      "uniform highp vec4 light_amb;  \n"
      "uniform highp float spec_power ; \n"

      "void main(void) { \n"

      "   vec3 L = light_pos.xyz - fP.xyz; \n"
      "   vec3 V = -fP.xyz; \n"
      "   vec3 N; \n"
      "   if(fN == vec3(0.0,0.0,0.0)) \n"
      "       N = vec3(0.0,0.0,0.0); \n"
      "   else \n"
      "       N = normalize(fN); \n"
      "   L = normalize(L); \n"
      "   V = normalize(V); \n"
      "   vec3 R = reflect(-L, N); \n"
      "   vec4 diffuse = max(abs(dot(N,L)),0.0) * light_diff*color; \n"
      "   vec4 specular = pow(max(dot(R,V), 0.0), spec_power) * light_spec; \n"

      "gl_FragColor = color*light_amb + diffuse + specular; \n"
      "} \n"
      "\n"
      };

      QOpenGLShader *vertex_shader = new QOpenGLShader(QOpenGLShader::Vertex);
      if(!vertex_shader->compileSourceCode(vertex_source))
      {
          std::cerr<<"Compiling vertex source FAILED"<<std::endl;
      }

      QOpenGLShader *fragment_shader= new QOpenGLShader(QOpenGLShader::Fragment);
      if(!fragment_shader->compileSourceCode(fragment_source))
      {
          std::cerr<<"Compiling fragmentsource FAILED"<<std::endl;
      }

      if(!rendering_program.addShader(vertex_shader))
      {
          std::cerr<<"adding vertex shader FAILED"<<std::endl;
      }
      if(!rendering_program.addShader(fragment_shader))
      {
          std::cerr<<"adding fragment shader FAILED"<<std::endl;
      }
      if(!rendering_program.link())
      {
          //std::cerr<<"linking Program FAILED"<<std::endl;
          qDebug() << rendering_program.log();
      }
  //setting the program used for the distance
     {
         vao[1].create();
         buffers[3].create();
         //Vertex source code
         const char vertex_source_dist[] =
         {
             "#version 120 \n"
             "attribute highp vec4 vertex;\n"
             "uniform highp mat4 mvp_matrix;\n"
             "void main(void)\n"
             "{\n"
             "   gl_Position = mvp_matrix * vertex; \n"
             "} \n"
             "\n"
         };
         //Fragment source code
         const char fragment_source_dist[] =
         {
             "#version 120 \n"
             "void main(void) { \n"
             "gl_FragColor = vec4(0.0,0.0,0.0,1.0); \n"
             "} \n"
             "\n"
         };
         vertex_shader = new QOpenGLShader(QOpenGLShader::Vertex);
         if(!vertex_shader->compileSourceCode(vertex_source_dist))
         {
             std::cerr<<"Compiling vertex source FAILED"<<std::endl;
         }

         fragment_shader= new QOpenGLShader(QOpenGLShader::Fragment);
         if(!fragment_shader->compileSourceCode(fragment_source_dist))
         {
             std::cerr<<"Compiling fragmentsource FAILED"<<std::endl;
         }

         if(!rendering_program_dist.addShader(vertex_shader))
         {
             std::cerr<<"adding vertex shader FAILED"<<std::endl;
         }
         if(!rendering_program_dist.addShader(fragment_shader))
         {
             std::cerr<<"adding fragment shader FAILED"<<std::endl;
         }
         if(!rendering_program_dist.link())
         {
             qDebug() << rendering_program_dist.log();
         }
     }

  d->painter = new QPainter(this);
}

#include <QMouseEvent>

void Viewer::mousePressEvent(QMouseEvent* event)
{
  if(event->button() == Qt::RightButton &&
     event->modifiers().testFlag(Qt::ShiftModifier)) 
  {
    select(event->pos());
    requestContextMenu(event->globalPos());
    event->accept();
  }
  else if(!event->modifiers()
          && event->button() == Qt::LeftButton
          && i_is_pressed)
  {
      d->scene->printPrimitiveId(event->pos(), this);
  }
  else if(!event->modifiers()
          && event->button() == Qt::LeftButton
          && is_d_pressed)
  {
      showDistance(event->pos());
      event->accept();
  }
  else {
    QGLViewer::mousePressEvent(event);
  }
}

#include <QContextMenuEvent>
void Viewer::contextMenuEvent(QContextMenuEvent* event)
{
  if(event->reason() != QContextMenuEvent::Mouse) {
    requestContextMenu(event->globalPos());
    event->accept();
  }
  else {
    QGLViewer::contextMenuEvent(event);
  }
}

void Viewer::keyPressEvent(QKeyEvent* e)
{
  if(!e->modifiers()) {
    if(e->key() == Qt::Key_T) {
      turnCameraBy180Degres();
      return;
    }
    else if(e->key() == Qt::Key_M) {
      d->macro_mode = ! d->macro_mode;

      if(d->macro_mode) {
          camera()->setZNearCoefficient(0.0005f);
      } else {
        camera()->setZNearCoefficient(0.005f);
      }
      this->displayMessage(tr("Macro mode: %1").
                           arg(d->macro_mode ? tr("on") : tr("off")));



      return;
    }
    else if(e->key() == Qt::Key_A) {
          axis_are_displayed = !axis_are_displayed;
          update();
        }
    else if(e->key() == Qt::Key_I) {
          i_is_pressed = true;
        }
    else if(e->key() == Qt::Key_D) {
        if(e->isAutoRepeat())
        {
            return;
        }
        if(!is_d_pressed)
        {
            clearDistancedisplay();
        }
        is_d_pressed = true;
        update();
        return;
    }
  }
  else if(e->key() == Qt::Key_I && e->modifiers() & Qt::ControlModifier){
    d->scene->printPrimitiveIds(this);
    update();
      }
  //forward the event to the scene (item handling of the event)
  if (! d->scene->keyPressEvent(e) )
    QGLViewer::keyPressEvent(e);
}

void Viewer::keyReleaseEvent(QKeyEvent *e)
{
  if(e->key() == Qt::Key_I) {
    i_is_pressed = false;
  }
  else if(!e->modifiers() && e->key() == Qt::Key_D)
  {
    if(e->isAutoRepeat())
    {
      return;
    }
    is_d_pressed = false;
  }
  QGLViewer::keyReleaseEvent(e);
}

void Viewer::turnCameraBy180Degres() {
  qglviewer::Camera* camera = this->camera();
  using qglviewer::ManipulatedCameraFrame;

  ManipulatedCameraFrame frame_from(*camera->frame());
  camera->setViewDirection(-camera->viewDirection());
  ManipulatedCameraFrame frame_to(*camera->frame());

  camera->setOrientation(frame_from.orientation());
  camera->interpolateTo(frame_to, 0.5f);
}

void Viewer_impl::draw_aux(bool with_names, Viewer* viewer)
{
  if(scene == 0)
    return;
  viewer->glLineWidth(1.0f);
  viewer->glPointSize(2.f);
  viewer->glEnable(GL_POLYGON_OFFSET_FILL);
  viewer->glPolygonOffset(1.0f,1.0f);
  viewer->glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);

  viewer->glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

  if(twosides)
    viewer->glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
  else
    viewer->glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);

  if(!with_names && antialiasing)
  {
    viewer->glEnable(GL_BLEND);
    viewer->glEnable(GL_LINE_SMOOTH);
    viewer->glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    viewer->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  else
  {
    viewer->glDisable(GL_BLEND);
    viewer->glDisable(GL_LINE_SMOOTH);
    viewer->glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);
    viewer->glBlendFunc(GL_ONE, GL_ZERO);
  }
  inDrawWithNames = with_names;
  if(with_names)
    scene->drawWithNames(viewer);
  else
    scene->draw(viewer);
  viewer->glDisable(GL_POLYGON_OFFSET_FILL);
  viewer->glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
}

bool Viewer::inDrawWithNames() const {
  return d->inDrawWithNames;
}

void Viewer::drawWithNames()
{
  QGLViewer::draw();
  d->draw_aux(true, this);
}

void Viewer::postSelection(const QPoint& pixel)
{
  Q_EMIT selected(this->selectedName());
  bool found = false;
  qglviewer::Vec point = camera()->pointUnderPixel(pixel, found);
  if(found) {
    Q_EMIT selectedPoint(point.x,
                       point.y,
                       point.z);
    const qglviewer::Vec orig = camera()->position();
    const qglviewer::Vec dir = point - orig;
    Q_EMIT selectionRay(orig.x, orig.y, orig.z,
                      dir.x, dir.y, dir.z);
  }
}
bool CGAL::Three::Viewer_interface::readFrame(QString s, qglviewer::Frame& frame)
{
  QStringList list = s.split(" ", QString::SkipEmptyParts);
  if(list.size() != 7)
    return false;
  float vec[3];
  for(int i = 0; i < 3; ++i)
  {
    bool ok;
    vec[i] = list[i].toFloat(&ok);
    if(!ok) return false;
  }
  double orient[4];
  for(int i = 0; i < 4; ++i)
  {
    bool ok;
    orient[i] = list[i + 3].toDouble(&ok);
    if(!ok) return false;
  }
  frame.setPosition(qglviewer::Vec(vec[0],
                                   vec[1],
                                   vec[2]));
  frame.setOrientation(orient[0],
                       orient[1],
                       orient[2],
                       orient[3]);
  return true;
}

QString CGAL::Three::Viewer_interface::dumpFrame(const qglviewer::Frame& frame) {
  const qglviewer::Vec pos = frame.position();
  const qglviewer::Quaternion q = frame.orientation();

  return QString("%1 %2 %3 %4 %5 %6 %7")
    .arg(pos[0])
    .arg(pos[1])
    .arg(pos[2])
    .arg(q[0])
    .arg(q[1])
    .arg(q[2])
    .arg(q[3]);
}

bool Viewer::moveCameraToCoordinates(QString s, float animation_duration) {
  qglviewer::Frame new_frame;
  if(readFrame(s, new_frame)) {
    camera()->interpolateTo(new_frame, animation_duration); 
    return true;
  }
  else
    return false;
}

QString Viewer::dumpCameraCoordinates()
{
  if(camera()->frame()) {
    return dumpFrame(*camera()->frame());
  } else {
    return QString();
  }
}

void Viewer::attribBuffers(int program_name) const {
    GLint is_both_sides = 0;
    //ModelViewMatrix used for the transformation of the camera.
    QMatrix4x4 mvp_mat;
    // ModelView Matrix used for the lighting system
    QMatrix4x4 mv_mat;
    // transformation of the manipulated frame
    QMatrix4x4 f_mat;

    f_mat.setToIdentity();
    //fills the MVP and MV matrices.
    GLdouble d_mat[16];
    this->camera()->getModelViewProjectionMatrix(d_mat);
    //Convert the GLdoubles matrices in GLfloats
    for (int i=0; i<16; ++i){
        mvp_mat.data()[i] = GLfloat(d_mat[i]);
    }
    this->camera()->getModelViewMatrix(d_mat);
    for (int i=0; i<16; ++i)
        mv_mat.data()[i] = GLfloat(d_mat[i]);

    const_cast<Viewer*>(this)->glGetIntegerv(GL_LIGHT_MODEL_TWO_SIDE,
                                             &is_both_sides);

    QVector4D position(0.0f,0.0f,1.0f, 1.0f );
    QVector4D ambient(0.4f, 0.4f, 0.4f, 0.4f);
    // Diffuse
    QVector4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
    // Specular
    QVector4D specular(0.0f, 0.0f, 0.0f, 1.0f);
    QOpenGLShaderProgram* program = getShaderProgram(program_name);
    program->bind();
    program->setUniformValue("mvp_matrix", mvp_mat);
    switch(program_name)
    {
    case PROGRAM_WITH_LIGHT:
    case PROGRAM_C3T3:
    case PROGRAM_PLANE_TWO_FACES:
    case PROGRAM_INSTANCED:
    case PROGRAM_WITH_TEXTURE:
    case PROGRAM_CUTPLANE_SPHERES:
    case PROGRAM_SPHERES:
        program->setUniformValue("light_pos", position);
        program->setUniformValue("light_diff",diffuse);
        program->setUniformValue("light_spec", specular);
        program->setUniformValue("light_amb", ambient);
        program->setUniformValue("spec_power", 51.8f);
        program->setUniformValue("is_two_side", is_both_sides);
        break;
    }
    switch(program_name)
    {
    case PROGRAM_WITH_LIGHT:
    case PROGRAM_C3T3:
    case PROGRAM_PLANE_TWO_FACES:
    case PROGRAM_INSTANCED:
    case PROGRAM_CUTPLANE_SPHERES:
    case PROGRAM_SPHERES:
      program->setUniformValue("mv_matrix", mv_mat);
      break;
    case PROGRAM_WITHOUT_LIGHT:
      program->setUniformValue("f_matrix",f_mat);
      break;
    case PROGRAM_WITH_TEXTURE:
      program->setUniformValue("mv_matrix", mv_mat);
      program->setUniformValue("s_texture",0);
      program->setUniformValue("f_matrix",f_mat);
      break;
    case PROGRAM_WITH_TEXTURED_EDGES:
        program->setUniformValue("s_texture",0);
        break;
    case PROGRAM_NO_SELECTION:
        program->setUniformValue("f_matrix",f_mat);
        break;
    }
    program->release();
}

void Viewer::beginSelection(const QPoint &point)
{
    makeCurrent();
    glEnable(GL_SCISSOR_TEST);
    glScissor(point.x(), camera()->screenHeight()-1-point.y(), 1, 1);
    d->scene->setPickedPixel(point);
}
void Viewer::endSelection(const QPoint&)
{
    glDisable(GL_SCISSOR_TEST);
    //redraw thetrue scene for the glReadPixel in postSelection();
    update();
}

void Viewer::makeArrow(double R, int prec, qglviewer::Vec from, qglviewer::Vec to, qglviewer::Vec color, AxisData &data)
{
    qglviewer::Vec temp = to-from;
    QVector3D dir = QVector3D(temp.x, temp.y, temp.z);
    QMatrix4x4 mat;
    mat.setToIdentity();
    mat.translate(from.x, from.y, from.z);
    mat.scale(dir.length());
    dir.normalize();
    float angle = 0.0;
    if(std::sqrt((dir.x()*dir.x()+dir.y()*dir.y())) > 1)
        angle = 90.0f;
    else
        angle =acos(dir.y()/std::sqrt(dir.x()*dir.x()+dir.y()*dir.y()+dir.z()*dir.z()))*180.0/M_PI;

    QVector3D axis;
    axis = QVector3D(dir.z(), 0, -dir.x());
    mat.rotate(angle, axis);

    //Head
    const float Rf = static_cast<float>(R);
    for(int d = 0; d<360; d+= 360/prec)
    {
        float D = (float) (d * M_PI / 180.);
        float a = (float) std::atan(Rf / 0.33);
        QVector4D p(0., 1., 0, 1.);
        QVector4D n(Rf*2.*sin(D), sin(a), Rf*2.*cos(D), 1.);
        QVector4D pR = mat*p;
        QVector4D nR = mat*n;

        //point A1
        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back((float)color.x);
        data.colors->push_back((float)color.y);
        data.colors->push_back((float)color.z);

        //point B1
        p = QVector4D(Rf*2.*sin(D), 0.66f, Rf*2.* cos(D), 1.f);
        n = QVector4D(sin(D), sin(a), cos(D), 1.);
        pR = mat*p;
        nR = mat*n;
        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back((float)color.x);
        data.colors->push_back((float)color.y);
        data.colors->push_back((float)color.z);
        //point C1
        D = (d+360/prec)*M_PI/180.0;
        p = QVector4D(Rf*2.* sin(D), 0.66f, Rf *2.* cos(D), 1.f);
        n = QVector4D(sin(D), sin(a), cos(D), 1.0);
        pR = mat*p;
        nR = mat*n;

        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back((float)color.x);
        data.colors->push_back((float)color.y);
        data.colors->push_back((float)color.z);

    }

    //cylinder
    //body of the cylinder
    for(int d = 0; d<360; d+= 360/prec)
    {
        //point A1
        double D = d*M_PI/180.0;
        QVector4D p(Rf*sin(D), 0.66f, Rf*cos(D), 1.f);
        QVector4D n(sin(D), 0.f, cos(D), 1.f);
        QVector4D pR = mat*p;
        QVector4D nR = mat*n;

        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back(color.x);
        data.colors->push_back(color.y);
        data.colors->push_back(color.z);
        //point B1
        p = QVector4D(Rf * sin(D),0,Rf*cos(D), 1.0);
        n = QVector4D(sin(D), 0, cos(D), 1.0);
        pR = mat*p;
        nR = mat*n;


        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back(color.x);
        data.colors->push_back(color.y);
        data.colors->push_back(color.z);
          //point C1
        D = (d+360/prec)*M_PI/180.0;
        p = QVector4D(Rf * sin(D),0,Rf*cos(D), 1.0);
        n = QVector4D(sin(D), 0, cos(D), 1.0);
        pR = mat*p;
        nR = mat*n;
        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back(color.x);
        data.colors->push_back(color.y);
        data.colors->push_back(color.z);
        //point A2
        D = (d+360/prec)*M_PI/180.0;

        p = QVector4D(Rf * sin(D),0,Rf*cos(D), 1.0);
        n = QVector4D(sin(D), 0, cos(D), 1.0);
        pR = mat*p;
        nR = mat*n;
        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back((float)color.x);
        data.colors->push_back((float)color.y);
        data.colors->push_back((float)color.z);
        //point B2
        p = QVector4D(Rf * sin(D), 0.66f, Rf*cos(D), 1.f);
        n = QVector4D(sin(D), 0, cos(D), 1.0);
        pR = mat*p;
        nR = mat*n;
        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back((float)color.x);
        data.colors->push_back((float)color.y);
        data.colors->push_back((float)color.z);
        //point C2
        D = d*M_PI/180.0;
        p = QVector4D(Rf * sin(D), 0.66f, Rf*cos(D), 1.f);
        n = QVector4D(sin(D), 0.f, cos(D), 1.f);
        pR = mat*p;
        nR = mat*n;
        data.vertices->push_back(pR.x());
        data.vertices->push_back(pR.y());
        data.vertices->push_back(pR.z());
        data.normals->push_back(nR.x());
        data.normals->push_back(nR.y());
        data.normals->push_back(nR.z());
        data.colors->push_back(color.x);
        data.colors->push_back(color.y);
        data.colors->push_back(color.z);

    }
}

void Viewer::drawVisualHints()
{

    QGLViewer::drawVisualHints();
    if(axis_are_displayed)
    {
        QMatrix4x4 mvpMatrix;
        double mat[16];
        QMatrix4x4 mvMatrix;
        camera()->getModelViewProjectionMatrix(mat);
        //nullifies the translation
        mat[12]=0;
        mat[13]=0;
        mat[14]=0;
        for(int i=0; i < 16; i++)
        {
            mvpMatrix.data()[i] = (float)mat[i];
        }
        camera()->getModelViewMatrix(mat);
        for(int i=0; i < 16; i++)
        {
            mvMatrix.data()[i] = (float)mat[i];
        }

        QVector4D	position(0.0f,0.0f,1.0f,1.0f );
        // define material
        QVector4D	ambient;
        QVector4D	diffuse;
        QVector4D	specular;
        GLfloat      shininess ;
        // Ambient
        ambient[0] = 0.29225f;
        ambient[1] = 0.29225f;
        ambient[2] = 0.29225f;
        ambient[3] = 1.0f;
        // Diffuse
        diffuse[0] = 0.50754f;
        diffuse[1] = 0.50754f;
        diffuse[2] = 0.50754f;
        diffuse[3] = 1.0f;
        // Specular
        specular[0] = 0.0f;
        specular[1] = 0.0f;
        specular[2] = 0.0f;
        specular[3] = 0.0f;
        // Shininess
        shininess = 51.2f;

        rendering_program.bind();
        rendering_program.setUniformValue("light_pos", position);
        rendering_program.setUniformValue("mvp_matrix", mvpMatrix);
        rendering_program.setUniformValue("mv_matrix", mvMatrix);
        rendering_program.setUniformValue("light_diff", diffuse);
        rendering_program.setUniformValue("light_spec", specular);
        rendering_program.setUniformValue("light_amb", ambient);
        rendering_program.setUniformValue("spec_power", shininess);
        rendering_program.release();

        vao[0].bind();
        rendering_program.bind();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v_Axis.size() / 3));
        rendering_program.release();
        vao[0].release();
    }

    if(distance_is_displayed)
    {
        glDisable(GL_DEPTH_TEST);

        glLineWidth(3.0f);
        glPointSize(6.0f);
        //draws the distance
        QMatrix4x4 mvpMatrix;
        double mat[16];
        //camera()->frame()->rotation().getMatrix(mat);
        camera()->getModelViewProjectionMatrix(mat);
        //nullifies the translation
        for(int i=0; i < 16; i++)
        {
            mvpMatrix.data()[i] = (float)mat[i];
        }
        rendering_program_dist.bind();
        rendering_program_dist.setUniformValue("mvp_matrix", mvpMatrix);
        vao[1].bind();
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(2));
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(2));
        vao[1].release();
        rendering_program_dist.release();
        glEnable(GL_DEPTH_TEST);
        glPointSize(1.0f);
        glLineWidth(1.0f);

    }
    if (!d->painter->isActive())
      d->painter->begin(this);
    //So that the text is drawn in front of everything
    d->painter->beginNativePainting();
    glDisable(GL_DEPTH_TEST);
    d->painter->endNativePainting();
    //prints FPS
    TextItem *fps_text = new TextItem(20, int(1.5*((QApplication::font().pixelSize()>0)?QApplication::font().pixelSize():QApplication::font().pointSize())),0,fpsString,false, QFont(), Qt::gray);
    if(FPSIsDisplayed())
    {
      textRenderer->addText(fps_text);
    }
    //Prints the displayMessage
    QFont font = QFont();
    QFontMetrics fm(font);
    TextItem *message_text = new TextItem(10 + fm.width(message)/2, height()-20, 0, message, false, QFont(), Qt::gray );
    if (_displayMessage)
    {
      textRenderer->addText(message_text);
    }
    textRenderer->draw(this);
    if(FPSIsDisplayed())
      textRenderer->removeText(fps_text);
    if (_displayMessage)
      textRenderer->removeText(message_text);
}

void Viewer::resizeGL(int w, int h)
{
    QGLViewer::resizeGL(w,h);
    qglviewer::Vec dim = qglviewer::Vec(w,h, 0) ;
    GLdouble ortho[16];
    QMatrix4x4 orthoMatrix;
    ortho[0]  = 1.0/width(); ortho[1]  = 0; ortho[2]  = 0; ortho[3]  = -0.0;
    ortho[4]  = 0; ortho[5]  = 1.0/height(); ortho[6]  = 0; ortho[7]  = -0.0;
    ortho[8]  = 0; ortho[9]  = 0; ortho[10] = 2.0/(camera()->zNear()-camera()->zFar()); ortho[11] = -(camera()->zNear()+camera()->zFar())/(-camera()->zNear()+camera()->zFar());
    ortho[12] = 0; ortho[13] = 0; ortho[14] = 0; ortho[15] = 1;
    for(int i=0; i < 16; i++)
    {
        orthoMatrix.data()[i] = (float)ortho[i];
    }

    QVector4D length(60,60,60, 1.0);
    length = orthoMatrix * length;
    AxisData data;
    v_Axis.resize(0);
    n_Axis.resize(0);
    c_Axis.resize(0);
    data.vertices = &v_Axis;
    data.normals = &n_Axis;
    data.colors = &c_Axis;
    double l = length.x()*w/h;
    makeArrow(0.06,10, qglviewer::Vec(0,0,0),qglviewer::Vec(l,0,0),qglviewer::Vec(1,0,0), data);
    makeArrow(0.06,10, qglviewer::Vec(0,0,0),qglviewer::Vec(0,l,0),qglviewer::Vec(0,1,0), data);
    makeArrow(0.06,10, qglviewer::Vec(0,0,0),qglviewer::Vec(0,0,l),qglviewer::Vec(0,0,1), data);


    vao[0].bind();
    buffers[0].bind();
    buffers[0].allocate(v_Axis.data(), static_cast<int>(v_Axis.size()) * sizeof(float));
    rendering_program.enableAttributeArray("vertex");
    rendering_program.setAttributeBuffer("vertex",GL_FLOAT,0,3);
    buffers[0].release();

    buffers[1].bind();
    buffers[1].allocate(n_Axis.data(), static_cast<int>(n_Axis.size() * sizeof(float)));
    rendering_program.enableAttributeArray("normal");
    rendering_program.setAttributeBuffer("normal",GL_FLOAT,0,3);
    buffers[1].release();

    buffers[2].bind();
    buffers[2].allocate(c_Axis.data(), static_cast<int>(c_Axis.size() * sizeof(float)));
    rendering_program.enableAttributeArray("colors");
    rendering_program.setAttributeBuffer("colors",GL_FLOAT,0,3);
    buffers[2].release();

    rendering_program.release();
    vao[0].release();



    rendering_program.bind();
    rendering_program.setUniformValue("width", (float)dim.x);
    rendering_program.setUniformValue("height", (float)dim.y);
    rendering_program.setUniformValue("ortho_mat", orthoMatrix);
    rendering_program.release();

}

QOpenGLShaderProgram* Viewer::getShaderProgram(int name) const
{
    // workaround constness issues in Qt
    Viewer* viewer = const_cast<Viewer*>(this);

    switch(name)
    {
    /// @TODO: factorize this code   
    case PROGRAM_C3T3:
        if(d->shader_programs[PROGRAM_C3T3])
        {
            return d->shader_programs[PROGRAM_C3T3];
        }

        else
        {

            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_c3t3.v"))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_c3t3.f"))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("colors", 1);
            program->link();
            d->shader_programs[PROGRAM_C3T3] = program;
            return program;
        }
        break;
    case PROGRAM_C3T3_EDGES:
        if(d->shader_programs[PROGRAM_C3T3_EDGES])
        {
            return d->shader_programs[PROGRAM_C3T3_EDGES];
        }

        else
        {

            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_c3t3_edges.v"))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_c3t3_edges.f"))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("colors", 1);
            program->link();
            d->shader_programs[PROGRAM_C3T3_EDGES] = program;
            return program;
        }
        break;
    case PROGRAM_WITH_LIGHT:
        if(d->shader_programs[PROGRAM_WITH_LIGHT])
        {
            return d->shader_programs[PROGRAM_WITH_LIGHT];
        }

        else
        {

            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_with_light.v"))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_with_light.f"))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("colors", 1);
            program->link();
            d->shader_programs[PROGRAM_WITH_LIGHT] = program;
            return program;
        }
        break;
    case PROGRAM_WITHOUT_LIGHT:
        if( d->shader_programs[PROGRAM_WITHOUT_LIGHT])
        {
            return d->shader_programs[PROGRAM_WITHOUT_LIGHT];
        }
        else
        {
            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_without_light.v"))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_without_light.f"))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("colors", 1);
            program->link();
            d->shader_programs[PROGRAM_WITHOUT_LIGHT] = program;
            return program;
        }
        break;
    case PROGRAM_NO_SELECTION:
        if( d->shader_programs[PROGRAM_NO_SELECTION])
        {
            return d->shader_programs[PROGRAM_NO_SELECTION];
        }
        else
        {
            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_without_light.v"))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_no_light_no_selection.f"))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("colors", 1);
            program->link();
            d->shader_programs[PROGRAM_NO_SELECTION] = program;
            return program;
        }
        break;
    case PROGRAM_WITH_TEXTURE:
        if( d->shader_programs[PROGRAM_WITH_TEXTURE])
        {
            return d->shader_programs[PROGRAM_WITH_TEXTURE];
        }
        else
        {
            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_with_texture.v"))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_with_texture.f"))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("color_facets", 1);
            program->link();
            d->shader_programs[PROGRAM_WITH_TEXTURE] = program;
            return program;
        }
        break;
    case PROGRAM_PLANE_TWO_FACES:
        if(d->shader_programs[PROGRAM_PLANE_TWO_FACES])
        {
            return d->shader_programs[PROGRAM_PLANE_TWO_FACES];
        }

        else
        {

            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_without_light.v"))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_plane_two_faces.f"))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->link();
            d->shader_programs[PROGRAM_PLANE_TWO_FACES] = program;
            return program;
        }
        break;

    case PROGRAM_WITH_TEXTURED_EDGES:
        if( d->shader_programs[PROGRAM_WITH_TEXTURED_EDGES])
        {
            return d->shader_programs[PROGRAM_WITH_TEXTURED_EDGES];
        }
        else
        {
            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_with_textured_edges.v" ))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_with_textured_edges.f" ))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("color_lines", 1);
            program->link();
            d->shader_programs[PROGRAM_WITH_TEXTURED_EDGES] = program;
            return program;

        }
        break;
    case PROGRAM_INSTANCED:
        if( d->shader_programs[PROGRAM_INSTANCED])
        {
            return d->shader_programs[PROGRAM_INSTANCED];
        }
        else
        {
            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_instanced.v" ))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_with_light.f" ))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("colors", 1);
            program->link();
            d->shader_programs[PROGRAM_INSTANCED] = program;
            return program;

        }
        break;
    case PROGRAM_INSTANCED_WIRE:
        if( d->shader_programs[PROGRAM_INSTANCED_WIRE])
        {
            return d->shader_programs[PROGRAM_INSTANCED_WIRE];
        }
        else
        {
            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_instanced.v" ))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_without_light.f" ))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("colors", 1);
            program->link();
            d->shader_programs[PROGRAM_INSTANCED_WIRE] = program;
            return program;

        }
        break;
    case PROGRAM_CUTPLANE_SPHERES:
      if( d->shader_programs[PROGRAM_CUTPLANE_SPHERES])
      {
          return d->shader_programs[PROGRAM_CUTPLANE_SPHERES];
      }
      else
      {
        QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
        if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_c3t3_spheres.v"))
        {
            std::cerr<<"adding vertex shader FAILED"<<std::endl;
        }
        if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_c3t3.f" ))
        {
            std::cerr<<"adding fragment shader FAILED"<<std::endl;
        }
        program->bindAttributeLocation("colors", 1);
        program->link();
        d->shader_programs[PROGRAM_CUTPLANE_SPHERES] = program;
        return program;
      }
    case PROGRAM_SPHERES:
        if( d->shader_programs[PROGRAM_SPHERES])
        {
            return d->shader_programs[PROGRAM_SPHERES];
        }
        else
        {
            QOpenGLShaderProgram *program = new QOpenGLShaderProgram(viewer);
            if(!program->addShaderFromSourceFile(QOpenGLShader::Vertex,":/cgal/Polyhedron_3/resources/shader_spheres.v" ))
            {
                std::cerr<<"adding vertex shader FAILED"<<std::endl;
            }
            if(!program->addShaderFromSourceFile(QOpenGLShader::Fragment,":/cgal/Polyhedron_3/resources/shader_with_light.f" ))
            {
                std::cerr<<"adding fragment shader FAILED"<<std::endl;
            }
            program->bindAttributeLocation("colors", 1);
            program->link();
            d->shader_programs[PROGRAM_SPHERES] = program;
            return program;

        }
      break;

    default:
        std::cerr<<"ERROR : Program not found."<<std::endl;
        return 0;
    }
}
void Viewer::wheelEvent(QWheelEvent* e)
{
    if(e->modifiers().testFlag(Qt::ShiftModifier))
    {
        double delta = e->delta();
        if(delta>0)
        {
            camera()->setZNearCoefficient(camera()->zNearCoefficient() * 1.01);
        }
        else
            camera()->setZNearCoefficient(camera()->zNearCoefficient() / 1.01);
        update();
    }
    else
        QGLViewer::wheelEvent(e);
}

bool Viewer::testDisplayId(double x, double y, double z)
{
    return d->scene->testDisplayId(x,y,z,this);
}

QPainter* Viewer::getPainter(){return d->painter;}

void Viewer::paintEvent(QPaintEvent *)
{
    paintGL();
}

void Viewer::paintGL()
{
  if (!d->painter->isActive())
    d->painter->begin(this);
  d->painter->beginNativePainting();
  glClearColor(backgroundColor().redF(), backgroundColor().greenF(), backgroundColor().blueF(), 1.0);
  preDraw();
  draw();
  postDraw();
  d->painter->endNativePainting();
  d->painter->end();
}

void Viewer::postDraw()
{

#ifdef GL_RESCALE_NORMAL  // OpenGL 1.2 Only...
  glEnable(GL_RESCALE_NORMAL);
#endif

  if (cameraIsEdited())
    camera()->drawAllPaths();

  // Pivot point, line when camera rolls, zoom region
  drawVisualHints();

  if (gridIsDrawn()) { glLineWidth(1.0); drawGrid(camera()->sceneRadius()); }
  if (axisIsDrawn()) { glLineWidth(2.0); drawAxis(camera()->sceneRadius()); }

  // FPS computation
  const unsigned int maxCounter = 20;
  if (++fpsCounter == maxCounter)
  {
    f_p_s = 1000.0 * maxCounter / fpsTime.restart();
    fpsString = tr("%1Hz", "Frames per seconds, in Hertz").arg(f_p_s, 0, 'f', ((f_p_s < 10.0)?1:0));
    fpsCounter = 0;
  }

}
void Viewer::displayMessage(const QString &_message, int delay)
{
          message = _message;
          _displayMessage = true;
          // Was set to single shot in defaultConstructor.
          messageTimer.start(delay);
          if (textIsEnabled())
                  update();
}
void Viewer::hideMessage()
{
        _displayMessage = false;
        if (textIsEnabled())
                update();
}
void Viewer::printMessage(QString _message, int ms_delay)
{
  displayMessage(_message, ms_delay);
}

void Viewer::showDistance(QPoint pixel)
{
    static bool isAset = false;
    bool found;
    qglviewer::Vec point;
    point = camera()->pointUnderPixel(pixel, found);
    if(!isAset && found)
    {
        //set APoint
        APoint = point;
        isAset = true;
        clearDistancedisplay();
    }
    else if (found)
    {
        //set BPoint
        BPoint = point;
        isAset = false;

        // fills the buffers
        std::vector<float> v;
        v.resize(6);
        v[0] = APoint.x; v[1] = APoint.y; v[2] = APoint.z;
        v[3] = BPoint.x; v[4] = BPoint.y; v[5] = BPoint.z;
        rendering_program_dist.bind();
        vao[1].bind();
        buffers[3].bind();
        buffers[3].allocate(v.data(),6*sizeof(float));
        rendering_program_dist.enableAttributeArray("vertex");
        rendering_program_dist.setAttributeBuffer("vertex",GL_FLOAT,0,3);
        buffers[3].release();
        vao[1].release();
        rendering_program_dist.release();
        distance_is_displayed = true;
        double dist = std::sqrt((BPoint.x-APoint.x)*(BPoint.x-APoint.x) + (BPoint.y-APoint.y)*(BPoint.y-APoint.y) + (BPoint.z-APoint.z)*(BPoint.z-APoint.z));
        QFont font;
        font.setBold(true);
        TextItem *ACoord = new TextItem(APoint.x, APoint.y, APoint.z,QString("A(%1,%2,%3)").arg(APoint.x).arg(APoint.y).arg(APoint.z), true, font, Qt::red, true);
        distance_text.append(ACoord);
        TextItem *BCoord = new TextItem(BPoint.x, BPoint.y, BPoint.z,QString("B(%1,%2,%3)").arg(BPoint.x).arg(BPoint.y).arg(BPoint.z), true, font, Qt::red, true);
        distance_text.append(BCoord);
        qglviewer::Vec centerPoint = 0.5*(BPoint+APoint);
        TextItem *centerCoord = new TextItem(centerPoint.x, centerPoint.y, centerPoint.z,QString(" distance: %1").arg(dist), true, font, Qt::red, true);

        distance_text.append(centerCoord);
        Q_FOREACH(TextItem* ti, distance_text)
          textRenderer->addText(ti);
        Q_EMIT(sendMessage(QString("First point : A(%1,%2,%3), second point : B(%4,%5,%6), distance between them : %7")
                  .arg(APoint.x)
                  .arg(APoint.y)
                  .arg(APoint.z)
                  .arg(BPoint.x)
                  .arg(BPoint.y)
                  .arg(BPoint.z)
                  .arg(dist)));
    }

}

void Viewer::clearDistancedisplay()
{
  distance_is_displayed = false;
  Q_FOREACH(TextItem* ti, distance_text)
  {
    textRenderer->removeText(ti);
    delete ti;
  }
  distance_text.clear();
}
