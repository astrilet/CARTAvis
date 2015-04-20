/**
 *
 **/

#include <QMetaType>
#include <QImage>

#pragma once

namespace Carta
{
namespace Lib
{
namespace VectorGraphics
{
class VGList
{
public:

    VGList();
    ~VGList();

    void setQImage( QImage img);
    QImage qImage();

private:

    QImage m_qImage;
};
}
}
}

Q_DECLARE_METATYPE( Carta::Lib::VectorGraphics::VGList)
