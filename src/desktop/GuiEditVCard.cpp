//////////////////////////////////////////////////////////////////////
//
// BeeBEEP Copyright (C) 2010-2026 Marco Mastroddi
//
// BeeBEEP is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
//
// BeeBEEP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with BeeBEEP. If not, see <http://www.gnu.org/licenses/>.
//
// Author: Marco Mastroddi <marco.mastroddi(AT)gmail.com>
//
// $Id: GuiEditVCard.cpp 1592 2026-08-11 10:14:22Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#include "Avatar.h"
#include "BeeUtils.h"
#include "FileDialog.h"
#include "GuiEditVCard.h"
#include "IconManager.h"
#include "Settings.h"
#include "UserManager.h"


namespace {

class AvatarCropView : public QWidget
{
public:
  AvatarCropView( const QImage& image, QWidget* parent )
    : QWidget( parent ), m_image( image )
  {
    const int side = qMin( image.width(), image.height() );
    m_crop = QRect( (image.width() - side) / 2, (image.height() - side) / 2, side, side );
    setMinimumSize( 180, 140 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setFocusPolicy( Qt::StrongFocus );
    setCursor( Qt::OpenHandCursor );
  }

  QSize sizeHint() const override { return QSize( 460, 320 ); }

  QImage croppedAvatar() const
  {
    return m_image.copy( m_crop ).scaled( 96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation );
  }

  void setCropSize( int side )
  {
    const QPointF center = QRectF( m_crop ).center();
    m_crop.setSize( QSize( side, side ) );
    moveCrop( center - QPointF( side / 2.0, side / 2.0 ) );
  }

protected:
  void resizeEvent( QResizeEvent* ) override
  {
    QSize pixels = (imageArea().size() * devicePixelRatioF()).toSize().expandedTo( QSize( 1, 1 ) );
    m_preview = m_image.scaled( pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
  }

  void paintEvent( QPaintEvent* ) override
  {
    QPainter painter( this );
    painter.fillRect( rect(), QColor( 36, 36, 36 ) );
    const QRectF area = imageArea();
    painter.drawImage( area, m_preview );
    const qreal scale = area.width() / m_image.width();
    const QRectF frame( area.topLeft() + QPointF( m_crop.topLeft() ) * scale, QSizeF( m_crop.size() ) * scale );
    QPainterPath shade;
    shade.addRect( area );
    shade.addRect( frame );
    painter.fillPath( shade, QColor( 0, 0, 0, 125 ) );
    painter.setPen( QPen( Qt::black, 3 ) );
    painter.drawRect( frame );
    painter.setPen( QPen( Qt::white, 1 ) );
    painter.drawRect( frame );
  }

  void mousePressEvent( QMouseEvent* event ) override
  {
    if( event->button() != Qt::LeftButton || !imageArea().contains( event->pos() ) )
      return;
    const QPointF point = imagePoint( event->pos() );
    m_dragOffset = QRectF( m_crop ).contains( point ) ? point - m_crop.topLeft()
                                                      : QPointF( m_crop.width() / 2.0, m_crop.height() / 2.0 );
    m_dragging = true;
    setCursor( Qt::ClosedHandCursor );
    moveCrop( point - m_dragOffset );
  }

  void mouseMoveEvent( QMouseEvent* event ) override
  {
    if( m_dragging )
      moveCrop( imagePoint( event->pos() ) - m_dragOffset );
  }

  void mouseReleaseEvent( QMouseEvent* event ) override
  {
    if( m_dragging && event->button() == Qt::LeftButton )
    {
      moveCrop( imagePoint( event->pos() ) - m_dragOffset );
      m_dragging = false;
      setCursor( Qt::OpenHandCursor );
    }
  }

  void keyPressEvent( QKeyEvent* event ) override
  {
    QPoint offset;
    switch( event->key() )
    {
    case Qt::Key_Left:  offset.setX( -1 ); break;
    case Qt::Key_Right: offset.setX( 1 ); break;
    case Qt::Key_Up:    offset.setY( -1 ); break;
    case Qt::Key_Down:  offset.setY( 1 ); break;
    default: QWidget::keyPressEvent( event ); return;
    }
    moveCrop( m_crop.topLeft() + offset * (event->modifiers() & Qt::ShiftModifier ? 10 : 1) );
  }

private:
  QRectF imageArea() const
  {
    QRectF bounds = QRectF( rect() ).adjusted( 4, 4, -4, -4 );
    QSizeF fitted( m_image.size() );
    fitted.scale( bounds.size(), Qt::KeepAspectRatio );
    QRectF area( QPointF(), fitted );
    area.moveCenter( bounds.center() );
    return area;
  }

  QPointF imagePoint( const QPointF& point ) const
  {
    const QRectF area = imageArea();
    return (point - area.topLeft()) * (m_image.width() / area.width());
  }

  void moveCrop( const QPointF& origin )
  {
    m_crop.moveTo( qBound( 0, qRound( origin.x() ), m_image.width() - m_crop.width() ),
                   qBound( 0, qRound( origin.y() ), m_image.height() - m_crop.height() ) );
    update();
  }

  QImage m_image;
  QImage m_preview;
  QRect m_crop;
  QPointF m_dragOffset;
  bool m_dragging = false;
};

} // namespace

GuiEditVCard::GuiEditVCard( QWidget *parent )
  : QDialog( parent )
{
  setupUi( this );
  setObjectName( "GuiEditVCard" );
  setWindowTitle( tr( "Edit your profile" ) + QString( " - %1" ).arg( Settings::instance().programName() ) );
  setWindowIcon( IconManager::instance().icon( "profile-edit.png" ) );
  Bee::removeContextHelpButton( this );

  connect( mp_pbOk, &QPushButton::clicked, this, &GuiEditVCard::onOkClicked );
  connect( mp_pbCancel, &QPushButton::clicked, this, &QDialog::reject );
  connect( mp_pbChangePhoto, &QPushButton::clicked, this, &GuiEditVCard::changePhoto );
  connect( mp_pbRemovePhoto, &QPushButton::clicked, this, &GuiEditVCard::removePhoto );
  connect( mp_leNickname, &QLineEdit::textEdited, this, &GuiEditVCard::refreshPreview );
}

void GuiEditVCard::setUser( const User& u )
{
  m_vCard = u.vCard();
  m_userColor = u.color();
  mp_leNickname->setMaxLength( qMax( 50, static_cast<int>( u.name().size() ) ) );
  mp_leNickname->setText( u.name() );
  bool can_edit_name = Settings::instance().allowEditNickname();
  mp_leNickname->setReadOnly( !can_edit_name );
  mp_leNickname->setToolTip( can_edit_name ? QString() : tr( "Disabled by system administrator" ) );
  refreshPreview();
  mp_leNickname->setFocus();
}

void GuiEditVCard::refreshPreview()
{
  QString name = mp_leNickname->text().simplified();
  bool has_photo = !m_vCard.photo().isNull();
  mp_lPhoto->setPixmap( has_photo ? m_vCard.photo().scaled( mp_lPhoto->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation )
                                  : Avatar::create( name.isEmpty() ? QStringLiteral( "?" ) : name, m_userColor, mp_lPhoto->size() ) );
  mp_pbRemovePhoto->setEnabled( has_photo );
  mp_pbOk->setEnabled( !name.isEmpty() );
}

void GuiEditVCard::changePhoto()
{
  QList<QByteArray> supported_formats = QImageReader::supportedImageFormats();
  QString supported_formats_string = "";

  foreach( QByteArray sf, supported_formats )
  {
    supported_formats_string += QString( " *." );
    supported_formats_string.append( sf );
  }

  QString photo_path = FileDialog::getOpenFileName( true, this, tr( "%1 - Select your profile photo" ).arg( Settings::instance().programName() ),
                                                     Settings::instance().lastDirectorySelected(), tr( "Images" ) + QString( " (%1)" ).arg( supported_formats_string.simplified() ) );
  if( photo_path.isEmpty() )
    return;

  Settings::instance().setLastDirectorySelectedFromFile( photo_path );

  QImage img;
  QImageReader img_reader( photo_path );
  img_reader.setAutoTransform( true );
  if( !img_reader.read( &img ) )
  {
    QMessageBox::warning( this, Settings::instance().programName(), tr( "Unable to load image %1." ).arg( photo_path ) );
    qWarning() << "Can not load profile picture. Format supported:" << supported_formats_string;
    return;
  }

  QDialog crop_dialog( this );
  crop_dialog.setWindowTitle( tr( "Crop avatar" ) );
  Bee::removeContextHelpButton( &crop_dialog );
  QVBoxLayout* layout = new QVBoxLayout( &crop_dialog );
  layout->addWidget( new QLabel( tr( "Drag the square to choose the part of the photo to use." ), &crop_dialog ) );
  AvatarCropView* view = new AvatarCropView( img, &crop_dialog );
  view->setAccessibleName( tr( "Crop avatar" ) );
  layout->addWidget( view, 1 );

  QHBoxLayout* size_layout = new QHBoxLayout();
  QLabel* size_label = new QLabel( tr( "Selection size" ), &crop_dialog );
  QSlider* size_slider = new QSlider( Qt::Horizontal, &crop_dialog );
  size_slider->setRange( 1, qMin( img.width(), img.height() ) );
  size_slider->setValue( size_slider->maximum() );
  size_slider->setSingleStep( qMax( 1, size_slider->maximum() / 100 ) );
  size_slider->setPageStep( qMax( 1, size_slider->maximum() / 10 ) );
  size_label->setBuddy( size_slider );
  size_layout->addWidget( size_label );
  size_layout->addWidget( size_slider, 1 );
  layout->addLayout( size_layout );
  connect( size_slider, &QSlider::valueChanged, view, &AvatarCropView::setCropSize );

  QDialogButtonBox* buttons = new QDialogButtonBox( &crop_dialog );
  QPushButton* apply = buttons->addButton( tr( "Use selection" ), QDialogButtonBox::AcceptRole );
  apply->setDefault( true );
  buttons->addButton( QCoreApplication::translate( "GuiVCardDialog", "Cancel" ), QDialogButtonBox::RejectRole );
  connect( buttons, &QDialogButtonBox::accepted, &crop_dialog, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, &crop_dialog, &QDialog::reject );
  layout->addWidget( buttons );
  view->setFocus();
  if( crop_dialog.exec() != QDialog::Accepted )
    return;

  m_vCard.setPhoto( QPixmap::fromImage( view->croppedAvatar() ) );
  refreshPreview();
}

void GuiEditVCard::removePhoto()
{
  m_vCard.setPhoto( QPixmap() );
  refreshPreview();
}

void GuiEditVCard::onOkClicked()
{
  QString name = mp_leNickname->text().simplified();
  if( name.isEmpty() )
    return;
  if( name != m_vCard.nickName() )
  {
    User u = UserManager::instance().findUserByNickname( name );
    if( u.isValid() && !u.isLocal() )
    {
      QMessageBox::warning( this, Settings::instance().programName(), tr( "The nickname '%1' is already in use by the user %2." ).arg( name, u.path() ) );
      mp_leNickname->setFocus();
      return;
    }
    m_vCard.setNickName( name );
  }
  accept();
}
