#include "Colormap.h"
#include "Colormaps.h"
#include "Settings.h"
#include "Data/Controller.h"
#include "TransformsData.h"
#include "Data/IColoredView.h"
#include "Data/Util.h"
#include "State/StateInterface.h"
#include "State/UtilState.h"
#include "ImageView.h"
#include "CartaLib/PixelPipeline/IPixelPipeline.h"
#include <QtCore/qmath.h>
#include <set>

#include <QDebug>

namespace Carta {

namespace Data {

const QString Colormap::CLASS_NAME = "Colormap";
const QString Colormap::COLOR_MAP_NAME = "colorMapName";
const QString Colormap::COLORED_OBJECT = "coloredObject";
const QString Colormap::REVERSE = "reverse";
const QString Colormap::INVERT = "invert";
const QString Colormap::COLOR_MIX = "colorMix";
const QString Colormap::RED_PERCENT = "redPercent";
const QString Colormap::GREEN_PERCENT = "greenPercent";
const QString Colormap::BLUE_PERCENT = "bluePercent";
const QString Colormap::COLOR_MIX_RED = COLOR_MIX + "/" + RED_PERCENT;
const QString Colormap::COLOR_MIX_GREEN = COLOR_MIX + "/" + GREEN_PERCENT;
const QString Colormap::COLOR_MIX_BLUE = COLOR_MIX + "/" + BLUE_PERCENT;
const QString Colormap::SCALE_1 = "scale1";
const QString Colormap::SCALE_2 = "scale2";
const QString Colormap::GAMMA = "gamma";
const QString Colormap::SIGNIFICANT_DIGITS = "significantDigits";
const QString Colormap::TRANSFORM_IMAGE = "imageTransform";
const QString Colormap::TRANSFORM_DATA = "dataTransform";

Colormaps* Colormap::m_colors = nullptr;
TransformsData* Colormap::m_dataTransforms = nullptr;

class Colormap::Factory : public Carta::State::CartaObjectFactory {

    public:

    Carta::State::CartaObject * create (const QString & path, const QString & id)
        {
            return new Colormap (path, id);
        }
    };

bool Colormap::m_registered =
        Carta::State::ObjectManager::objectManager()->registerClass ( CLASS_NAME, new Colormap::Factory());

Colormap::Colormap( const QString& path, const QString& id):
    CartaObject( CLASS_NAME, path, id ),
    m_linkImpl( new LinkableImpl( path ) ),
    m_stateMouse(Carta::State::UtilState::getLookup(path,ImageView::VIEW)){

    Carta::State::CartaObject* prefObj = Util::createObject( Settings::CLASS_NAME );
    m_settings.reset(dynamic_cast<Settings*>(prefObj) );

    _initializeDefaultState();
    _initializeCallbacks();
    _initializeStatics();
    _setErrorMargin();
}

QString Colormap::addLink( CartaObject*  cartaObject ){
    Controller* target = dynamic_cast<Controller*>(cartaObject);
    bool objAdded = false;
    QString result;
    if ( target != nullptr ){
        objAdded = m_linkImpl->addLink( target );
        if ( objAdded ){
            _setColorProperties( target );
            connect( target, SIGNAL(dataChanged(Controller*)), this, SLOT(_setColorProperties(Controller*)));
        }
    }
    else {
        result = "Color map only supports linking to images.";
    }
    return result;
}

QString Colormap::getPreferencesId() const {
    return m_settings->getPath();
}

QString Colormap::getStateString( const QString& /*sessionId*/, SnapshotType type ) const{
    QString result("");
    if ( type == SNAPSHOT_PREFERENCES ){
        result = m_state.toString();
    }
    else if ( type == SNAPSHOT_LAYOUT ){
        result = m_linkImpl->getStateString(getIndex(), getType( type ));
    }
    return result;
}

void Colormap::_initializeDefaultState(){
    m_state.insertValue<QString>( COLOR_MAP_NAME, "Gray" );
    m_state.insertValue<bool>(REVERSE, false);
    m_state.insertValue<bool>(INVERT, false );

    m_state.insertValue<double>(GAMMA, 1.0 );
    m_state.insertValue<double>(SCALE_1, 0.0 );
    m_state.insertValue<double>(SCALE_2, 0.0 );

    m_state.insertObject( COLOR_MIX );
    m_state.insertValue<double>( COLOR_MIX_RED, 1 );
    m_state.insertValue<double>(COLOR_MIX_GREEN, 1 );
    m_state.insertValue<double>(COLOR_MIX_BLUE, 1 );

    m_state.insertValue<int>(SIGNIFICANT_DIGITS, 6 );
    m_state.insertValue<QString>(TRANSFORM_IMAGE, "Gamma");
    m_state.insertValue<QString>(TRANSFORM_DATA, "None");
    m_state.flushState();

    m_stateMouse.insertObject( ImageView::MOUSE );
    m_stateMouse.insertValue<QString>(ImageView::MOUSE_X, 0 );
    m_stateMouse.insertValue<QString>(ImageView::MOUSE_Y, 0 );
    m_stateMouse.flushState();

}

void Colormap::_initializeCallbacks(){

    addCommandCallback( "registerPreferences", [=] (const QString & /*cmd*/,
                        const QString & /*params*/, const QString & /*sessionId*/) -> QString {
                    QString result = getPreferencesId();
                    return result;
    });


    addCommandCallback( "setDataTransform", [=] (const QString & /*cmd*/,
                                const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {TRANSFORM_DATA};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString dataTransformStr = dataValues[*keys.begin()];
        QString result = setDataTransform( dataTransformStr );
        return result;
   });

    addCommandCallback( "setColormap", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
            QString result = _commandSetColorMap( params );
            return result;
        });

    addCommandCallback( "invertColormap", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
            QString result = _commandInvertColorMap( params );
            return result;
        });

    addCommandCallback( "reverseColormap", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
            QString result = _commandReverseColorMap( params );
            return result;
        });

    addCommandCallback( "setColorMix", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
                QString result = _commandSetColorMix( params );
                return result;
            });

    addCommandCallback( "setScales", [=] (const QString & /*cmd*/,
                            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {SCALE_1, SCALE_2};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        bool valid = false;
        double scale1 = dataValues[SCALE_1].toDouble( & valid );
        if ( !valid ){
            result = "Invalid color scale: "+params;
        }
        else {
            double scale2 = dataValues[SCALE_2].toDouble( &valid );
            if ( !valid ){
                result = "Invalid color scale: "+params;
            }
            else {
                double oldScale1 = m_state.getValue<double>(SCALE_1);
                double oldScale2 = m_state.getValue<double>(SCALE_2);
                bool changedState = false;
                if ( scale1 != oldScale1 ){
                    m_state.setValue<double>(SCALE_1, scale1 );
                    changedState = true;
                }
                if ( scale2 != oldScale2 ){
                    m_state.setValue<double>(SCALE_2, scale2 );
                    changedState = true;
                }
                if ( changedState ){
                    m_state.flushState();
                    //Calculate the new gamma
                    double maxndig = 10;
                    double ndig = (scale2 + 1) / 2 * maxndig;
                    double expo = std::pow( 2.0, ndig);
                    double xx = std::pow(scale1 * 2, 3) / 8.0;
                    double gamma = fabs(xx) * expo + 1;
                    if( scale1 < 0){
                        gamma = 1 / gamma;
                    }
                    result = setGamma( gamma );
                }
            }
        }
        return result;
    });

    addCommandCallback( "setSignificantDigits", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
                QString result;
                std::set<QString> keys = {SIGNIFICANT_DIGITS};
                std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
                QString digitsStr = dataValues[SIGNIFICANT_DIGITS];
                bool validDigits = false;
                int digits = digitsStr.toInt( &validDigits );
                if ( validDigits ){
                    result = setSignificantDigits( digits );
                }
                else {
                    result = "Colormap significant digits must be an integer: "+params;
                }
                Util::commandPostProcess( result );
                return result;
            });
}

void Colormap::_initializeStatics(){
    //Load the available color maps.
    if ( m_colors == nullptr ){
        CartaObject* obj = Util::findSingletonObject( Colormaps::CLASS_NAME );
        m_colors = dynamic_cast<Colormaps*>( obj );
    }

    //Data transforms
    if ( m_dataTransforms == nullptr ){
        CartaObject* obj = Util::findSingletonObject( TransformsData::CLASS_NAME );
        m_dataTransforms =dynamic_cast<TransformsData*>( obj );
    }
}

void Colormap::clear(){
    m_linkImpl->clear();
}



QString Colormap::_commandInvertColorMap( const QString& params ){
    QString result;
    std::set<QString> keys = {INVERT};
    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    QString invertStr = dataValues[*keys.begin()];
    bool validBool = false;
    bool invert = Util::toBool( invertStr, &validBool );
    if ( validBool ){
        result = invertColorMap( invert );
    }
    else {
        result = "Invert color map parameters must be true/false: "+params;
    }
    Util::commandPostProcess( result );
    return result;
}

QString Colormap::reverseColorMap( bool reverse ){
    QString result;
    bool oldReverse = m_state.getValue<bool>(REVERSE);
    if ( reverse != oldReverse ){
        m_state.setValue<bool>(REVERSE, reverse );
        m_state.flushState();
        int linkCount = m_linkImpl->getLinkCount();
        for( int i = 0; i < linkCount; i++ ){
            Controller* controller = dynamic_cast<Controller*>( m_linkImpl->getLink(i));
            controller -> setColorReversed( reverse );
        }
        emit colorMapChanged( this );
    }
    return result;
}

bool Colormap::isReversed() const {
    return m_state.getValue<bool>( REVERSE );
}

bool Colormap::isInverted() const {
    return m_state.getValue<bool>( INVERT );
}


QString Colormap::invertColorMap( bool invert ){
    QString result;
    bool oldInvert = m_state.getValue<bool>(INVERT );
    if ( invert != oldInvert ){
        m_state.setValue<bool>(INVERT, invert );
        m_state.flushState();
        int linkCount = m_linkImpl->getLinkCount();
        for( int i = 0; i < linkCount; i++ ){
            Controller* controller = dynamic_cast<Controller*>( m_linkImpl->getLink(i));
            controller -> setColorInverted ( invert );
        }
        emit colorMapChanged( this );
    }
    return result;
}

QString Colormap::_commandSetColorMix( const QString& params ){
    QString result;
    std::set<QString> keys = {RED_PERCENT, GREEN_PERCENT, BLUE_PERCENT};
    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );

    bool validRed = false;
    double redValue = dataValues[RED_PERCENT].toDouble(&validRed );

    bool validBlue = false;
    double blueValue = dataValues[BLUE_PERCENT].toDouble(&validBlue );

    bool validGreen = false;
    double greenValue = dataValues[GREEN_PERCENT].toDouble(&validGreen);

    if ( validRed && validBlue && validGreen ){
        result = setColorMix( redValue, blueValue, greenValue );
    }
    else {
        result = "Color mix values must be numbers: "+params;
    }
    Util::commandPostProcess( result );
    return result;
}

QString Colormap::setColorMix( double redValue, double blueValue, double greenValue ){
    QString result;
    bool greenChanged = _setColorMix( COLOR_MIX_GREEN, greenValue, result );
    bool redChanged = _setColorMix( COLOR_MIX_RED, redValue, result );
    bool blueChanged = _setColorMix( COLOR_MIX_BLUE, blueValue, result );
    if ( redChanged || blueChanged || greenChanged ){
        m_state.flushState();
        double newRed = m_state.getValue<double>(COLOR_MIX_RED );
        double newGreen = m_state.getValue<double>(COLOR_MIX_GREEN );
        double newBlue = m_state.getValue<double>(COLOR_MIX_BLUE );
        int linkCount = m_linkImpl->getLinkCount();
        for( int i = 0; i < linkCount; i++ ){
            Controller* controller = dynamic_cast<Controller*>( m_linkImpl->getLink(i));
            controller->setColorAmounts( newRed, newGreen, newBlue );
        }
        emit colorMapChanged( this );
    }
    return result;
}

QString Colormap::_commandReverseColorMap( const QString& params ){
    QString result;
    std::set<QString> keys = {REVERSE};
    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    QString reverseStr = dataValues[*keys.begin()];
    bool validBool = false;
    bool reverse = Util::toBool(reverseStr, &validBool);
    if ( validBool ){
        result = reverseColorMap( reverse );
    }
    else {
        result = "Invalid color map reverse parameters: "+ params;
    }
    Util::commandPostProcess( result );
    return result;
}



QString Colormap::_commandSetColorMap( const QString& params ){
    std::set<QString> keys = {"name"};
    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    QString colorMapStr = dataValues[*keys.begin()];
    QString result = setColorMap( colorMapStr );
    return result;
}


Controller* Colormap::getControllerSelected() const {
    //TODO:  Add sophistication.
    Controller* target = nullptr;
    int linkCount = m_linkImpl->getLinkCount();
    for ( int i = 0; i < linkCount; i++ ){
        CartaObject* obj = m_linkImpl->getLink(i);
        Controller* control = dynamic_cast<Controller*>(obj);
        if ( control != nullptr ){
            target = control;
            break;
        }
    }
    return target;
}

bool Colormap::_setColorMix( const QString& key, double colorPercent, QString& errorMsg ){
    bool colorChanged = false;
    if ( colorPercent<0 || colorPercent > 1 ){
        errorMsg = errorMsg + key + " mix must be in [0,1]. ";
    }
    else {
        double oldColorPercent = m_state.getValue<double>( key );
        if ( abs( colorPercent - oldColorPercent ) >= 0.001f ){
            m_state.setValue<double>(key, colorPercent );
            colorChanged = true;
        }
    }
    return colorChanged;
}



QString Colormap::removeLink( CartaObject* cartaObject ){
    Controller* controller = dynamic_cast<Controller*>(cartaObject);
    bool objRemoved = false;
    QString result;
    if ( controller != nullptr ){
        objRemoved = m_linkImpl->removeLink( controller );
        if ( objRemoved ){
            disconnect( controller );
        }
    }
    else {
        result = "Color map could not remove link; only links to images supported.";
    }
    return result;
}

QString Colormap::setColorMap( const QString& colorMapStr ){
    QString mapName = m_state.getValue<QString>(COLOR_MAP_NAME);
    QString result;
    if ( m_colors != nullptr ){
       if( m_colors->isMap( colorMapStr ) ){
           if ( colorMapStr != mapName ){
              m_state.setValue<QString>(COLOR_MAP_NAME, colorMapStr );
              m_state.flushState();
              int linkCount = m_linkImpl->getLinkCount();
              for( int i = 0; i < linkCount; i++ ){
                  Controller* controller = dynamic_cast<Controller*>( m_linkImpl->getLink(i));
                  controller->setColorMap( colorMapStr );
              }
              emit colorMapChanged( this );
           }
        }
       else {
           result = "Invalid colormap: " + colorMapStr;
       }
    }

    Util::commandPostProcess( result );
    return result;
}

QString Colormap::setGamma( double gamma ){
    QString result;
    double oldGamma = m_state.getValue<double>( GAMMA );
    if ( qAbs( gamma - oldGamma) > m_errorMargin ){
        int digits = m_state.getValue<int>(SIGNIFICANT_DIGITS);
        m_state.setValue<double>(GAMMA, Util::roundToDigits(gamma, digits ));
        m_state.flushState();
        //Let the controllers know gamma has changed.
        int linkCount = m_linkImpl->getLinkCount();
        for( int i = 0; i < linkCount; i++ ){
            Controller* controller = dynamic_cast<Controller*>( m_linkImpl->getLink(i));
            controller->setGamma( gamma );
        }
        emit colorMapChanged( this );
    }
    return result;
}

QString Colormap::setDataTransform( const QString& transformString ){
    QString transformName = m_state.getValue<QString>(TRANSFORM_DATA);
    QString result;
    if ( m_dataTransforms != nullptr ){
        QString actualTransform;
        bool recognizedTransform = m_dataTransforms->isTransform( transformString, actualTransform );
        if( recognizedTransform ){
            if ( actualTransform != transformName ){
                m_state.setValue<QString>(TRANSFORM_DATA, actualTransform );
                m_state.flushState();
                int linkCount = m_linkImpl->getLinkCount();
                for( int i = 0; i < linkCount; i++ ){
                    Controller* controller = dynamic_cast<Controller*>( m_linkImpl->getLink(i));
                    controller->setTransformData( actualTransform );
                }
                emit colorMapChanged( this );
            }
            else {
               result = "Invalid data transform: " + transformString;
            }
        }
    }
    Util::commandPostProcess( result );
    return result;
}

QList<QString> Colormap::getLinks() const {
    return m_linkImpl->getLinkIds();
}

void Colormap::refreshState(){
    CartaObject::refreshState();
    m_settings->refreshState();
    m_linkImpl->refreshState();
}

void Colormap::_setColorProperties( Controller* target ){
    target->setColorMap( m_state.getValue<QString>(COLOR_MAP_NAME));
    target->setColorInverted( m_state.getValue<bool>(INVERT) );
    target->setColorReversed( m_state.getValue<bool>(REVERSE) );
    double redPercent = m_state.getValue<double>(COLOR_MIX_RED);
    double greenPercent = m_state.getValue<double>(COLOR_MIX_GREEN );
    double bluePercent = m_state.getValue<double>(COLOR_MIX_BLUE);
    target->setColorAmounts( redPercent, greenPercent, bluePercent );
    target->setGamma( m_state.getValue<double>(GAMMA) );
    target->setTransformData( m_state.getValue<QString>(TRANSFORM_DATA ));
}

QString Colormap::setSignificantDigits( int digits ){
    QString result;
    if ( digits <= 0 ){
        result = "Invalid significant digits; must be positive:  "+QString::number( digits );
    }
    else {
        if ( m_state.getValue<int>(SIGNIFICANT_DIGITS) != digits ){
            m_state.setValue<int>(SIGNIFICANT_DIGITS, digits );
            _setErrorMargin();
        }
    }
    return result;
}

void Colormap::_setErrorMargin(){
    int significantDigits = m_state.getValue<int>(SIGNIFICANT_DIGITS );
    m_errorMargin = 1.0/qPow(10,significantDigits);
}

Colormap::~Colormap(){
    if ( m_settings != nullptr ){
        Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
        QString id = m_settings->getId();
        objMan->removeObject( id );
    }
}
}
}