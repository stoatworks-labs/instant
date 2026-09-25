#include "Instant.h"

#include "Controls.h"
#include "Diag.h"
#include "Model.h"
#include "Shaders.h"

#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ffglex;
using namespace instant;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Instant >,                                    // Create method
	"IN01",                                                      // Plugin unique ID of maximum length 4.
	"SW Instant",                                                // Plugin name
	2,                                                           // API major version number
	1,                                                           // API minor version number
	0,                                                           // Plugin major version number
	1,                                                           // Plugin minor version number
	FF_EFFECT,                                                   // Plugin type
	"Instant film, developing in front of you.\n\nA Take exposes the clip's frame onto an integral instant print. The rollers spread the reagent up from the pod at the bottom border, and the picture comes up over minutes from a dark green-grey, pale and cyan first, warming as the magenta and yellow dyes arrive: each dye layer first-order, at a rate set by an Arrhenius temperature law. Cold film is slow, pale and green, hot film fast and warm; a short spread leaves the corners dark, a dirty roller repeats its mark. Speed scales time for live use; 1x is a real print.",// Plugin description
	"Instant FFGL effect"                                        // About
);

namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;

/// A tiny slack on a time comparison, so a take that lands on its interval
/// to within double rounding is counted on the side it means.
constexpr double kSlack = 1e-9;

/// Wall clock, for hosts that never call SetTime. Steady rather than system,
/// so nothing here moves when the machine's clock is corrected.
double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

/// glGetString returns nullptr when there is no current context, and feeding
/// that to std::string is undefined behaviour.
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}
} // namespace

//---------------------------------------------------------------------------
Instant::Instant()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//Development runs on the host's clock: a re-render of the same
	//composition must develop the same print the same way.
	SetTimeSupported( true );

	//---------------------------------------------------------------------
	// Defaults. Colour film at 24 degC, fresh, a full spread and a clean
	// roller, the print in its frame; a new print every 16 s at 64x, so a
	// print comes up in a couple of seconds and is all but done by the next.
	//---------------------------------------------------------------------
	params[ PT_FILM ]        = 0.0f;//Colour
	params[ PT_TEMPERATURE ] = controls::TemperatureParam( model::kReferenceC );
	params[ PT_EXPIRED ]     = 0.0f;

	params[ PT_TAKE ]     = 0.0f;
	params[ PT_MODE ]     = static_cast< float >( controls::kModeContinuous );
	params[ PT_INTERVAL ] = controls::IntervalParam( 16.0 );
	params[ PT_EXPOSURE ] = 0.5f;

	params[ PT_SPREAD ] = static_cast< float >( controls::kSpreadFull );
	params[ PT_ROLLER ] = 0.0f;
	params[ PT_SPEED ]  = controls::SpeedParam( 64.0 );

	params[ PT_BORDER ] = 1.0f;
	params[ PT_MIX ]    = 1.0f;

	//---------------------------------------------------------------------
	// Declaration. Every ranged FF_TYPE_STANDARD parameter is a plain 0..1
	// float: SetParamInfo clamps a STANDARD default into 0..1 *before* a
	// range can be attached (SDK b1afaf9). The conversions live in
	// Controls.cpp. Option lists are in their natural order and not sorted.
	//---------------------------------------------------------------------
	auto declareOptions = [ this ]( unsigned int id, const char* name, int count, const char* ( *nameAt )( int ) ) {
		SetOptionParamInfo( id, name, static_cast< unsigned int >( count ), params[ id ] );
		for( int i = 0; i < count; ++i )
			SetParamElementInfo( id, static_cast< unsigned int >( i ), nameAt( i ), static_cast< float >( i ) );
	};

	declareOptions( PT_FILM, "Film", controls::kFilmCount, controls::FilmName );
	SetParamInfof( PT_TEMPERATURE, "Temperature", FF_TYPE_STANDARD );
	SetParamInfof( PT_EXPIRED, "Expired", FF_TYPE_STANDARD );

	SetParamInfo( PT_TAKE, "Take", FF_TYPE_EVENT, false );
	declareOptions( PT_MODE, "Mode", controls::kModeCount, controls::ModeName );
	SetParamInfof( PT_INTERVAL, "Interval", FF_TYPE_STANDARD );
	SetParamInfof( PT_EXPOSURE, "Exposure", FF_TYPE_STANDARD );

	declareOptions( PT_SPREAD, "Spread", controls::kSpreadCount, controls::SpreadName );
	SetParamInfof( PT_ROLLER, "Dirty Roller", FF_TYPE_STANDARD );
	SetParamInfof( PT_SPEED, "Speed", FF_TYPE_STANDARD );

	SetParamInfof( PT_BORDER, "Border", FF_TYPE_STANDARD );
	SetParamInfof( PT_MIX, "Mix", FF_TYPE_STANDARD );

	for( FFUInt32 i = PT_FILM; i <= PT_EXPIRED; ++i )
		SetParamGroup( i, "Film" );
	for( FFUInt32 i = PT_TAKE; i <= PT_EXPOSURE; ++i )
		SetParamGroup( i, "Exposure" );
	for( FFUInt32 i = PT_SPREAD; i <= PT_SPEED; ++i )
		SetParamGroup( i, "Chemistry" );
	for( FFUInt32 i = PT_BORDER; i <= PT_MIX; ++i )
		SetParamGroup( i, "Print" );

	// The About block. Inline rather than through a helper: SetParamInfo is
	// protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_FIRST; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );

	FFGLLog::LogToHost( "Created Instant effect" );

	diag::init();
}

//---------------------------------------------------------------------------
FFResult Instant::InitGL( const FFGLViewportStruct* vp )
{
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR )
	            + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	const std::string vertex = shaders::Vertex();
	struct
	{
		FFGLShader* shader;
		std::string fragment;
		const char* name;
	} const stages[] = {
		{ &captureShader, shaders::Capture(), "capture" },
		{ &meterShader, shaders::Meter(), "meter" },
		{ &developShader, shaders::Develop(), "develop" },
		{ &resampleShader, shaders::Resample(), "resample" },
		{ &printShader, shaders::Print(), "print" },
	};

	for( const auto& stage : stages )
	{
		if( stage.shader->Compile( vertex, stage.fragment ) )
			continue;

		//Returning FF_FAIL here is invisible to the operator: the effect
		//simply does nothing in Resolume, with no message anywhere. These two
		//lines are the only record of which pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the effect will do nothing" );
		FFGLLog::LogToHost( "Instant: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		FFGLLog::LogToHost( "Instant: quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	//A fresh context holds no print. takePending is deliberately left alone:
	//a press that arrived before the GL context existed is still a press
	//(wetplate's trap: clearing it here read as a dead control).
	hasPrint  = false;
	age       = 0.0;
	current   = 0;
	lastWidth = lastHeight = 0;

	diag::info( "initialised" );

	//Use base-class init as the success result so it retains the viewport.
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
FFResult Instant::SetTime( double time )
{
	hostTimeSeen = true;
	return CFFGLPlugin::SetTime( time );
}

//The unit voting is readout's, unchanged: the ratio of the host's clock
//delta to a steady clock's names the unit outright, and nothing plausible
//sits between 1 and 1000.
double Instant::nowSeconds()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;

	if( !hostTimeSeen || hostTime < 0.0 )
		return wallNow - wallStart;

	const double raw = hostTime;

	if( clockScale == 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;

		//A paused host, a looping clip or a stalled frame tells us nothing.
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;

			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
		}
	}
	lastRawTime  = raw;
	lastWallTime = wallNow;

	//Until the unit is settled, run on the real clock rather than assume one:
	//wrong in origin but right in rate, where assuming seconds would be a
	//thousand times fast on Resolume.
	return clockScale != 0.0 ? raw * clockScale : wallNow - wallStart;
}

//---------------------------------------------------------------------------
bool Instant::rescale( int width, int height )
{
	//Every allocation first, then the passes: allocating unbinds the active
	//texture unit, and a resample that ran between the two would read
	//nothing on the frame that mattered.
	struct Item
	{
		PassBuffer* buffer;
		GLint format;
	};
	const Item items[] = { { &capture, GL_RGBA16F }, { &dose[ current ], GL_RGBA32F } };
	for( const Item& item : items )
	{
		PassBuffer& b = *item.buffer;
		if( !b.IsValid() || ( static_cast< int >( b.GetWidth() ) == width && static_cast< int >( b.GetHeight() ) == height ) )
			continue;
		if( !scratch.Ensure( width, height, item.format, PassBuffer::Sampling::Linear ) )
			return false;
		{
			ScopedFBOBinding fbo( scratch.GetGLID(), ScopedFBOBinding::RB_REVERT );
			scratch.ResizeViewPort();
			ScopedShaderBinding shader( resampleShader.GetGLID() );
			ScopedSamplerActivation sampler( 0 );
			Scoped2DTextureBinding texture( b.TextureID() );
			resampleShader.Set( "Source", 0 );
			resampleShader.Set( "HalfTexel", 0.5f / static_cast< float >( b.GetWidth() ), 0.5f / static_cast< float >( b.GetHeight() ) );
			quad.Draw();
		}
		//The old buffer is now `scratch`, at the old size, and is released:
		//the next item may want another format, and between resizes there is
		//no reason to hold a fourth picture-sized buffer.
		b.Swap( scratch );
		scratch.Destroy();
	}
	return true;
}

//---------------------------------------------------------------------------
FFResult Instant::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	if( input.Width == 0 || input.Height == 0 )
		return FF_FAIL;

	const int width  = static_cast< int >( input.Width );
	const int height = static_cast< int >( input.Height );

	//The host's viewport, read before anything of ours changes it.
	//ScopedFBOBinding restores the framebuffer binding and only that.
	GLint hostViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, hostViewport );
	if( hostViewport[ 2 ] <= 0 || hostViewport[ 3 ] <= 0 )
	{
		hostViewport[ 2 ] = width;
		hostViewport[ 3 ] = height;
	}

	//---------------------------------------------------------------------
	// The clock. Everything that reads it -- when a print is taken, how many
	// film-seconds this frame is worth -- is reduced in double here; the
	// shaders see only the age at the frame's start and the frame's length,
	// as floats, both relative to the take.
	//---------------------------------------------------------------------
	const double now = nowSeconds();
	double dt        = model::kNominalFrame;
	if( lastNow >= 0.0 )
		dt = std::clamp( now - lastNow, 0.0, model::kMaxFrameDelta );
	lastNow = now;

	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( hostTime ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );

	//---------------------------------------------------------------------
	// What the controls say.
	//---------------------------------------------------------------------
	const int film          = controls::OptionIndex( params[ PT_FILM ], controls::kFilmCount );
	const double celsius    = controls::TemperatureC( params[ PT_TEMPERATURE ] );
	const float expired     = controls::Expired( params[ PT_EXPIRED ] );
	const int mode          = controls::OptionIndex( params[ PT_MODE ], controls::kModeCount );
	const double interval   = controls::IntervalSeconds( params[ PT_INTERVAL ] );
	const float stops       = controls::ExposureStops( params[ PT_EXPOSURE ] );
	const int spread        = controls::OptionIndex( params[ PT_SPREAD ], controls::kSpreadCount );
	const float roller      = controls::RollerStrength( params[ PT_ROLLER ] );
	const double speed      = controls::SpeedFactor( params[ PT_SPEED ] );
	const float border      = controls::Border( params[ PT_BORDER ] );
	const float mixAmount   = std::clamp( params[ PT_MIX ], 0.0f, 1.0f );

	//---------------------------------------------------------------------
	// The take. Continuous: the first frame, a press, or Interval host
	// seconds since the last take. Take: a press, and nothing else; before
	// the first one the plugin is a viewfinder. A host clock that runs
	// backwards (a restart) restarts the interval and keeps the print.
	//---------------------------------------------------------------------
	if( now < lastTake - kSlack )
		lastTake = now;
	bool take = takePending;
	if( mode == controls::kModeContinuous && ( !hasPrint || now - lastTake >= interval - kSlack ) )
		take = true;
	takePending = false;

	double ageStart = age;
	double dAge     = 0.0;
	if( take )
	{
		hasPrint = true;
		lastTake = now;
		age = ageStart = 0.0;
		++takes;
	}
	else if( hasPrint )
	{
		dAge = dt * speed;
		age += dAge;
	}

	//Each dye layer at its own activation energy; the black-and-white stock's
	//one image at the chart's. The negative control puts every layer back on
	//v0.1.0's shared figure.
	const bool noArrhenius = ( perturb & model::kPerturbNoArrhenius ) != 0;
	const bool sharedEa    = ( perturb & model::kPerturbSharedActivation ) != 0 || model::kStocks[ film ].mono;
	double kDye[ 3 ];
	for( int i = 0; i < 3; ++i )
		kDye[ i ] = noArrhenius ? 1.0 : controls::ArrheniusFactor( celsius, sharedEa ? model::kDyeActivation : model::kLayerActivation[ i ] );
	const double kStop = noArrhenius ? 1.0 : controls::ArrheniusFactor( celsius, model::kStopActivation );

	//---------------------------------------------------------------------
	// Buffers. Every allocation happens here, before anything binds a
	// texture: allocating leaves the active unit bound to nothing, and the
	// symptom of getting the order wrong is correct on every frame except
	// the one that allocates.
	//
	// A resize carries the print across, resampled, unless the negative
	// control says to let it clear: the photofinish trap, guarded by --take.
	//---------------------------------------------------------------------
	const bool resized = lastWidth != 0 && ( lastWidth != width || lastHeight != height );
	if( resized && hasPrint && !take && ( perturb & model::kPerturbResizeClears ) == 0 )
	{
		if( !rescale( width, height ) )
		{
			diag::error( "could not resample the print to " + std::to_string( width ) + "x" + std::to_string( height ) );
			return FF_FAIL;
		}
	}
	lastWidth  = width;
	lastHeight = height;

	if( !capture.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Linear )
	    || !dose[ 0 ].Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Linear )
	    || !dose[ 1 ].Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Linear )
	    || !meter.Ensure( model::kMeterSize, model::kMeterSize, GL_R32F, PassBuffer::Sampling::Mipmapped ) )
	{
		diag::error( "could not allocate the print buffers at " + std::to_string( width ) + "x" + std::to_string( height ) );
		return FF_FAIL;
	}
	if( resized )
		diag::info( "raster " + std::to_string( width ) + "x" + std::to_string( height ) + ", " + std::to_string( StateBytesForTest() / 1048576 ) + " MB of print state" );

	//A fresh print has no reagent anywhere.
	if( take )
	{
		dose[ current ].Clear();
		dose[ 1 - current ].Clear();
	}

	//---------------------------------------------------------------------
	// The layout and the crop: the image window shows the centre of the
	// clip's frame, cropped to the window's aspect.
	//---------------------------------------------------------------------
	const controls::Layout layout = controls::PrintLayout( hostViewport[ 2 ], hostViewport[ 3 ], border );
	const double iw = std::max( 1.0f, layout.imageMax[ 0 ] - layout.imageMin[ 0 ] );
	const double ih = std::max( 1.0f, layout.imageMax[ 1 ] - layout.imageMin[ 1 ] );
	const double windowAspect = iw / ih;
	const double sourceAspect = static_cast< double >( width ) / height;
	const float cropX = static_cast< float >( windowAspect < sourceAspect ? windowAspect / sourceAspect : 1.0 );
	const float cropY = static_cast< float >( windowAspect < sourceAspect ? 1.0 : sourceAspect / windowAspect );

	const FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );

	//---------------------------------------------------------------------
	// 1. Capture: the light at the take.
	//---------------------------------------------------------------------
	if( take || ( hasPrint && ( perturb & model::kPerturbLiveCapture ) != 0 ) )
	{
		ScopedFBOBinding fbo( capture.GetGLID(), ScopedFBOBinding::RB_REVERT );
		capture.ResizeViewPort();
		ScopedShaderBinding shader( captureShader.GetGLID() );
		ScopedSamplerActivation sampler( 0 );
		Scoped2DTextureBinding texture( input.Handle );
		captureShader.Set( "InputTexture", 0 );
		quad.Draw();
	}
	if( take )
	{
		//The meter reads what the film will see, at the take, once.
		{
			ScopedFBOBinding fbo( meter.GetGLID(), ScopedFBOBinding::RB_REVERT );
			meter.ResizeViewPort();
			ScopedShaderBinding shader( meterShader.GetGLID() );
			ScopedSamplerActivation sampler( 0 );
			Scoped2DTextureBinding texture( capture.TextureID() );
			meterShader.Set( "Capture", 0 );
			meterShader.Set( "CropScale", cropX, cropY );
			quad.Draw();
		}
		meter.GenerateMipmaps();
	}

	//---------------------------------------------------------------------
	// 2. Develop: this frame's film-seconds into the doses.
	//---------------------------------------------------------------------
	if( hasPrint && dAge > 0.0 )
	{
		PassBuffer& from = dose[ current ];
		PassBuffer& to   = dose[ 1 - current ];
		{
			ScopedFBOBinding fbo( to.GetGLID(), ScopedFBOBinding::RB_REVERT );
			to.ResizeViewPort();
			ScopedShaderBinding shader( developShader.GetGLID() );
			ScopedSamplerActivation sampler( 0 );
			Scoped2DTextureBinding texture( from.TextureID() );
			developShader.Set( "Previous", 0 );
			developShader.Set( "Size", static_cast< float >( width ), static_cast< float >( height ) );
			developShader.Set( "AgeStart", static_cast< float >( ageStart ) );
			developShader.Set( "DAge", static_cast< float >( dAge ) );
			developShader.Set( "KDye", static_cast< float >( kDye[ 0 ] ), static_cast< float >( kDye[ 1 ] ), static_cast< float >( kDye[ 2 ] ) );
			developShader.Set( "KStop", static_cast< float >( kStop ) );
			developShader.Set( "StopDose", static_cast< float >( model::kStopDose ) );
			developShader.Set( "CropScale", cropX, cropY );
			developShader.Set( "Spread", spread );
			developShader.Set( "Expired", expired );
			developShader.Set( "Perturb", perturb );
			quad.Draw();
		}
		current = 1 - current;
	}

	//---------------------------------------------------------------------
	// 3. The print, straight to the host.
	//---------------------------------------------------------------------
	{
		glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
		glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );

		//The stock, with Expired applied, and the balance that makes a
		//neutral grey come out neutral at the stop at 24 degC.
		const model::Stock& stock = model::kStocks[ film ];
		const double* tauStock    = film == 2 ? model::kTauVintage : model::kTauColour;
		double tau[ 3 ], balance[ 3 ], dmin[ 3 ], dmax[ 3 ];
		for( int i = 0; i < 3; ++i )
		{
			tau[ i ]     = stock.mono ? model::kTauMono : tauStock[ i ];
			balance[ i ] = 1.0 / ( 1.0 - std::exp( -model::kStopDose / tau[ i ] ) );
			const double loss = stock.mono ? model::kExpiredDmaxLoss : ( i == 0 ? model::kExpiredCyanLoss : model::kExpiredDmaxLoss );
			dmax[ i ] = stock.dmax[ i ] * ( 1.0 - loss * expired );
			dmin[ i ] = stock.dmin[ i ] + ( stock.mono ? 0.5 : ( i == 0 ? 0.0 : 1.0 ) ) * model::kExpiredStain * expired;
		}
		const float* white = stock.mono ? model::kWhiteMono : model::kWhiteColour;
		const float ox = static_cast< float >( hostViewport[ 0 ] ), oy = static_cast< float >( hostViewport[ 1 ] );

		ScopedShaderBinding shader( printShader.GetGLID() );
		ScopedSamplerActivation sampler0( 0 );
		Scoped2DTextureBinding doseTexture( dose[ current ].TextureID() );
		ScopedSamplerActivation sampler1( 1 );
		Scoped2DTextureBinding captureTexture( capture.TextureID() );
		ScopedSamplerActivation sampler2( 2 );
		Scoped2DTextureBinding sourceTexture( input.Handle );
		ScopedSamplerActivation sampler3( 3 );
		Scoped2DTextureBinding meterTexture( meter.TextureID() );

		printShader.Set( "Dose", 0 );
		printShader.Set( "Capture", 1 );
		printShader.Set( "Source", 2 );
		printShader.Set( "Meter", 3 );
		printShader.Set( "MeterLevel", static_cast< float >( model::kMeterLevel ) );
		printShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		printShader.Set( "PrintMin", ox + layout.printMin[ 0 ], oy + layout.printMin[ 1 ] );
		printShader.Set( "PrintMax", ox + layout.printMax[ 0 ], oy + layout.printMax[ 1 ] );
		printShader.Set( "ImageMin", ox + layout.imageMin[ 0 ], oy + layout.imageMin[ 1 ] );
		printShader.Set( "ImageMax", ox + layout.imageMax[ 0 ], oy + layout.imageMax[ 1 ] );
		printShader.Set( "CropScale", cropX, cropY );
		printShader.Set( "HasPrint", hasPrint ? 1 : 0 );
		printShader.Set( "Mono", stock.mono ? 1 : 0 );
		printShader.Set( "ExposureGain", std::exp2( stops ) );
		printShader.Set( "Toe", static_cast< float >( stock.shoulder - stock.latitude ) );
		printShader.Set( "Latitude", static_cast< float >( stock.latitude ) );
		printShader.Set( "Dmin", static_cast< float >( dmin[ 0 ] ), static_cast< float >( dmin[ 1 ] ), static_cast< float >( dmin[ 2 ] ) );
		printShader.Set( "Dmax", static_cast< float >( dmax[ 0 ] ), static_cast< float >( dmax[ 1 ] ), static_cast< float >( dmax[ 2 ] ) );
		printShader.Set( "Balance", static_cast< float >( balance[ 0 ] ), static_cast< float >( balance[ 1 ] ), static_cast< float >( balance[ 2 ] ) );
		printShader.Set( "Tau", static_cast< float >( tau[ 0 ] ), static_cast< float >( tau[ 1 ] ), static_cast< float >( tau[ 2 ] ) );
		printShader.Set( "TauOp", static_cast< float >( model::kTauOpacifier ) );
		printShader.Set( "White", white[ 0 ], white[ 1 ], white[ 2 ] );
		printShader.Set( "Roller", roller );
		printShader.Set( "Spread", spread );
		printShader.Set( "Expired", expired );
		printShader.Set( "MixAmount", mixAmount );
		printShader.Set( "Probe", probe );
		printShader.Set( "Perturb", perturb );
		quad.Draw();
	}

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Instant::DeInitGL()
{
	captureShader.FreeGLResources();
	meterShader.FreeGLResources();
	developShader.FreeGLResources();
	resampleShader.FreeGLResources();
	printShader.FreeGLResources();
	quad.Release();
	capture.Destroy();
	dose[ 0 ].Destroy();
	dose[ 1 ].Destroy();
	scratch.Destroy();
	meter.Destroy();
	hasPrint = false;

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Instant::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// An About button is a press, not a value to keep: it opens a browser and
	// nothing about the effect changes.
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_TAKE )
	{
		//An event arrives as 1.0 on press and 0.0 on release; the take is
		//the press. Edge-triggered, so a host restating 1.0 does not fire
		//it again.
		if( value >= 0.5f && takeWas < 0.5f )
			takePending = true;
		takeWas = value;
		return FF_SUCCESS;
	}

	params[ index ] = value;
	return FF_SUCCESS;
}

float Instant::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT )
		return 0.0f;

	return params[ index ];
}

//---------------------------------------------------------------------------
char* Instant::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}

	return CFFGLPlugin::GetTextParameter( index );
}

FFResult Instant::SetTextParameter( unsigned int index, const char* value )
{
	// See the declaration: the base class fails, and a failed default deletes
	// the instance. The About line is display-only, so there is genuinely
	// nothing to store -- but it has to say so successfully.
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;

	return CFFGLPlugin::SetTextParameter( index, value );
}

//---------------------------------------------------------------------------
void Instant::SetClockScaleForTest( double scale )
{
	clockScale = scale;
}

void Instant::SetPerturbForTest( int bits )
{
	perturb = bits;
}

void Instant::SetProbeForTest( int p )
{
	probe = p;
}

size_t Instant::StateBytesForTest() const
{
	size_t bytes = 0;
	auto count = [ &bytes ]( const PassBuffer& b, size_t perTexel ) {
		if( b.IsValid() )
			bytes += static_cast< size_t >( b.GetWidth() ) * b.GetHeight() * perTexel;
	};
	count( capture, 8 );
	count( dose[ 0 ], 16 );
	count( dose[ 1 ], 16 );
	return bytes;
}
