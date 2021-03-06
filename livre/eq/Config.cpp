/* Copyright (c) 2006-2016, Stefan Eilemann <eile@equalizergraphics.com>
 *                          Maxim Makhinya  <maxmah@gmail.com>
 *                          David Steiner   <steiner@ifi.uzh.ch>
 *
 * This file is part of Livre <https://github.com/BlueBrain/Livre>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <livre/eq/Config.h>

#include <livre/eq/Client.h>
#include <livre/eq/Event.h>
#include <livre/eq/EventHandler.h>
#include <livre/eq/FrameData.h>
#include <livre/eq/settings/CameraSettings.h>
#include <livre/eq/settings/FrameSettings.h>
#include <livre/eq/settings/RenderSettings.h>
#include <livre/eq/settings/VolumeSettings.h>
#include <livre/lib/configuration/ApplicationParameters.h>
#include <livre/lib/configuration/VolumeRendererParameters.h>

#include <livre/core/data/Histogram.h>
#include <livre/core/data/VolumeInformation.h>
#include <livre/core/util/FrameUtils.h>

#include <lexis/render/imageJPEG.h>

#ifdef LIVRE_USE_ZEROEQ
#  include <livre/eq/zeroeq/communicator.h>
#endif

#include <eq/eq.h>

#include <deque>
#include <functional>

namespace livre
{

class Config::Impl
{
public:

    typedef std::vector< Histogram > Histograms;

    struct ViewHistogram
    {
        ViewHistogram( const Histogram& histogram_,
                       const float area_,
                       const uint32_t id_ )
            : histogram( histogram_ )
            , area( area_ )
            , id( id_ )

        {}

        ViewHistogram& operator+=( const ViewHistogram& hist )
        {
            histogram += hist.histogram;
            area += hist.area;
            return *this;
        }

        bool isComplete() const
        {
            const float eps = 0.0001f;
            return std::abs( 1.0f - area ) <= eps;
        }

        Histogram histogram;
        float area;
        uint32_t id;

    };

    typedef std::deque< ViewHistogram > ViewHistogramQueue;

    explicit Impl( Config* config_ )
        : config( config_ )
        , latency( 0 )
        , redraw( true )
        , dataSourceRange( 0.0f, 255.0f ) // Default range for uint8 data sources
        , frameStart( config->getTime( ))
    {}

    void gatherHistogram( const Histogram& histogram, const float area, const uint32_t currentId )
    {
        // If we get a very old frame skip it.
        if( !histogramQueue.empty() && currentId < histogramQueue.back().id )
            return;

        // Extend the global histogram range if needed.
        if( histogram.getMin() < dataSourceRange[ 0 ] )
            dataSourceRange[ 0 ] = histogram.getMin();

        if( histogram.getMax() > dataSourceRange[ 1 ] )
            dataSourceRange[ 1 ] = histogram.getMax();

        // Updating the range that clients must use to set their histogram range
        config->getFrameData().getVolumeSettings().setDataSourceRange( dataSourceRange );

        const ViewHistogram viewHistogram( histogram, area, currentId );
        for( auto it = histogramQueue.begin(); it != histogramQueue.end(); )
        {
            bool dataMerged = false;

            if( currentId == it->id )
            {
                try
                {
                    *it += viewHistogram;
                    dataMerged = true;
                }
                catch( std::runtime_error& )
                {
                    // Only compatible histograms can be added.( i.e same data
                    // range and number of bins.) Until data range converges to
                    // the full data range combined from all rendering clients,
                    // the histograms are thrown away
                    histogramQueue.erase( it, histogramQueue.end( ));
                    return;
                }
            }
            else if( currentId > it->id )
            {
                dataMerged = true;
                it = histogramQueue.emplace( it, viewHistogram );
            }

            if( it->isComplete( )) // Send histogram & remove all old ones
            {
#ifdef LIVRE_USE_ZEROEQ
                communicator->publish( it->histogram );
#endif
                histogramQueue.erase( it, histogramQueue.end( ));
                return;
            }

            if( dataMerged )
                break;
            ++it;
        }

        if( histogramQueue.empty() && !viewHistogram.isComplete( ))
        {
            histogramQueue.push_back( viewHistogram );
            return;
        }

        if( viewHistogram.isComplete( ))
        {
#ifdef LIVRE_USE_ZEROEQ
            communicator->publish( viewHistogram.histogram );
#endif
            return;
        }

        if( histogramQueue.size() > latency + 1 )
            histogramQueue.pop_back();
    }

    Config* config;
    uint32_t latency;
    FrameData framedata;
#ifdef LIVRE_USE_ZEROEQ
    std::unique_ptr< zeroeq::Communicator > communicator;
#endif
    bool redraw;
    VolumeInformation volumeInfo;
    ViewHistogramQueue histogramQueue;
    Vector2f dataSourceRange;
    int64_t frameStart;
};

Config::Config( eq::ServerPtr parent )
    : EventHandler< eq::Config >( *this, parent )
    , _impl( new Impl( this ))
{
    _impl->framedata.initialize( this );
}

Config::~Config()
{
}

FrameData& Config::getFrameData()
{
    return _impl->framedata;
}

const FrameData& Config::getFrameData() const
{
    return _impl->framedata;
}

std::string Config::renderJPEG()
{
    getFrameData().getFrameSettings().setGrabFrame( true );
    frame();

    for( ;; )
    {
        eq::EventICommand event = getNextEvent();
        if( !event.isValid( ))
            continue;

        if( event.getEventType() == GRAB_IMAGE )
        {
            const uint64_t size = event.read< uint64_t >();
            const uint8_t* data = reinterpret_cast< const uint8_t* >(
                                      event.getRemainingBuffer( size ));

            ::lexis::render::ImageJPEG imageJPEG;
            imageJPEG.setData( data, size );
            return imageJPEG.toJSON();
        }

        handleEvent( event );
    }
    return "";
}

const VolumeInformation& Config::getVolumeInformation() const
{
    return _impl->volumeInfo;
}

VolumeInformation& Config::getVolumeInformation()
{
    return _impl->volumeInfo;
}

const ApplicationParameters& Config::getApplicationParameters() const
{
    return static_cast<const Client&>( *getClient()).getApplicationParameters();
}

ApplicationParameters& Config::getApplicationParameters()
{
    return static_cast< Client& >( *getClient()).getApplicationParameters();
}

void Config::mapFrameData( const eq::uint128_t& initId )
{
    _impl->framedata.map( this, initId );
    _impl->framedata.mapObjects( );
}

void Config::unmapFrameData()
{
    _impl->framedata.unmapObjects( );
    _impl->framedata.unmap( this );
}

void Config::resetCamera()
{
    _impl->framedata.getCameraSettings().setCameraPosition(
        getApplicationParameters().cameraPosition );
    _impl->framedata.getCameraSettings().setCameraLookAt(
        getApplicationParameters().cameraLookAt );
}

bool Config::init( const int argc LB_UNUSED, char** argv LB_UNUSED )
{
#ifdef LIVRE_USE_ZEROEQ
    _impl->communicator.reset( new zeroeq::Communicator( *this, argc, argv ));

    _impl->framedata.getCameraSettings().registerNotifyChanged(
                std::bind( &zeroeq::Communicator::publishCamera,
                           _impl->communicator.get(), std::placeholders::_1 ));
#endif

    resetCamera();
    FrameData& framedata = _impl->framedata;
    FrameSettings& frameSettings = framedata.getFrameSettings();
    const ApplicationParameters& params = getApplicationParameters();
    frameSettings.setFrameNumber( params.frames.x( ));

    RenderSettings& renderSettings = framedata.getRenderSettings();
    const TransferFunction1D tf( params.transferFunction );
    renderSettings.setTransferFunction( tf );

    _impl->framedata.registerObjects();

    if( !_impl->framedata.registerToConfig( this ))
        return false;

    if( !eq::Config::init( _impl->framedata.getID( )))
    {
        _impl->framedata.deregisterObjects();
        _impl->framedata.deregisterFromConfig( this );
        return false;
    }

    EventHandler< eq::Config >::init();
    _impl->latency = getLatency();
    return true;
}

bool Config::frame()
{
    if( _impl->volumeInfo.frameRange == INVALID_FRAME_RANGE )
        return false;

    ApplicationParameters& params = getApplicationParameters();
    FrameSettings& frameSettings = _impl->framedata.getFrameSettings();

    const FrameUtils frameUtils( params.frames, _impl->volumeInfo.frameRange );
    params.frames = frameUtils.getFrameRange();

    // Set current frame (start/end may have changed)
    const bool keepToLatest = params.animation == LATEST_FRAME;
    const uint32_t current =
        frameUtils.getCurrent( frameSettings.getFrameNumber(), keepToLatest );

    frameSettings.setFrameNumber( current );
    const eq::uint128_t& version = _impl->framedata.commit();

    if( _impl->framedata.getVRParameters().getSynchronousMode( ))
        setLatency( 0 );
    else
        setLatency( _impl->latency );

    // reset data and advance current frame
    frameSettings.setGrabFrame( false );

    if( !keepToLatest && !_keepCurrentFrame( params.animationFPS ))
    {
        frameSettings.setFrameNumber( frameUtils.getNext( current,
                                                          params.animation ));
        // reset starting time for new frame
         _impl->frameStart = getTime();
    }
    _impl->redraw = false;

#ifdef LIVRE_USE_ZEROEQ
    _impl->communicator->publishFrame();
#endif

    eq::Config::startFrame( version );
    eq::Config::finishFrame();
    return true;
}

void Config::postRedraw()
{
    _impl->redraw = true;
}

bool Config::needRedraw()
{
    return _impl->redraw || getApplicationParameters().animation != 0;
}

bool Config::publish( const servus::Serializable& serializable )
{
#ifdef LIVRE_USE_ZEROEQ
    return _impl->communicator->publish( serializable );
#else
    return false;
#endif
}

bool Config::exit()
{
    bool ret = eq::Config::exit(); // cppcheck-suppress unreachableCode
    _impl->framedata.deregisterObjects();
    if( !_impl->framedata.deregisterFromConfig( this ))
        ret = false;
    return ret;
}

void Config::handleNetworkEvents()
{
#ifdef LIVRE_USE_ZEROEQ
    _impl->communicator->handleEvents();
#endif
}

bool Config::_keepCurrentFrame( const uint32_t fps ) const
{
    if( fps == 0 )
        return false;

    const double desiredTime = 1.0 / fps;
    const int64_t end = getTime();

    // If the frame duration is shorter than the desired frame time then the
    // current frame should be kept until the duration matches (or exceeds) the
    // expected. Otherwise, the frame number should be normally increased.
    // This means that no frames are artificially skipped due to the fps limit
    const double frameDuration = ( end - _impl->frameStart ) / 1e3;
    return frameDuration < desiredTime;
}

}
