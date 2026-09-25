/**
	intest -- render Instant offline, and read the print back out of it.

	Every check here drives the REAL plugin class through a headless GL
	context on a synthetic 60 fps clock, and measures the answer out of the
	picture it made:

		intest --out /tmp/frame.png     a picture, on the moving test card
		intest --list                   every parameter, its kind and default
		intest --develop                each dye layer's density on a flat
		                                patch follows its first-order law with
		                                the stated tau, fitted without knowing
		                                the asymptote; the opacifier clears
		                                with its own
		intest --order                  at the stated early time the cyan
		                                layer leads and the print is blue-cyan,
		                                and the balance then moves monotonically
		                                to neutral at the stop
		intest --arrhenius              tau at 14 and 34 degC has the ratio
		                                the Arrhenius law gives, for every dye
		                                layer and for the timing layer
		intest --front                  a point s from the pod edge starts
		                                developing s / front speed later
		intest --roller                 the dirty roller's mark repeats at
		                                exactly its circumference, whole-pixel
		                                (the pattern translates exactly) and
		                                fractional (mark centroids)
		intest --take                   before a take in Take mode the output
		                                is the clip; after it the print keeps
		                                developing from the captured frame as
		                                the clip changes, and a resize keeps it
		intest --meter                  the camera's averaging meter: two patch
		                                levels print alike, at the stated
		                                density of mid grey; one past its range
		                                prints darker, as stated
		intest --negative               every check above can FAIL
		intest --names                  nothing the host will truncate
		intest --bench                  the render cost and the state held
		intest --dump-shaders DIR       the exact GLSL the plugin compiles
		intest --pipe                   raw frames in, raw frames out

	The laws are stated HERE, from their definitions (Controls.h's comments
	and Model.h), and read as constants from the same headers the plugin
	compiles; where a check can measure a property without the model's
	numbers (a time constant from the ratio of successive differences, a
	period from the picture translating onto itself) it does. AGENTS.md has
	one line per check on where each tolerance comes from.
*/

#include "Controls.h"
#include "Instant.h"
#include "Model.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
namespace model    = instant::model;
namespace controls = instant::controls;

int g_checks   = 0;
int g_failures = 0;

constexpr double kU  = 5.9604644775390625e-8;//2^-24, half a float ulp at 1
constexpr double kLn10 = 2.302585092994046;

//---------------------------------------------------------------------------
// A PNG writer. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );
		const unsigned char* row = rgba.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.push_back( 8 );
	ihdr.push_back( 6 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}


//---------------------------------------------------------------------------
// sRGB, in double.
//---------------------------------------------------------------------------
/// sRGB, both ways, in double, with NO clamp on the way in: the wedges feed
/// float textures above 1 and the plugin's decode has no clamp either.
double srgbEncode( double v )
{
	return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow( v, 1.0 / 2.4 ) - 0.055;
}
double srgbDecode( double v )
{
	return v <= 0.04045 ? v / 12.92 : std::pow( ( v + 0.055 ) / 1.055, 2.4 );
}


//---------------------------------------------------------------------------
// Pictures, float RGBA, top-first.
//---------------------------------------------------------------------------
using Picture = std::vector< float >;

Picture flat( int W, int H, double r, double g, double b )
{
	Picture p( static_cast< size_t >( W ) * H * 4 );
	for( size_t i = 0; i < p.size(); i += 4 )
	{
		p[ i ]     = static_cast< float >( r );
		p[ i + 1 ] = static_cast< float >( g );
		p[ i + 2 ] = static_cast< float >( b );
		p[ i + 3 ] = 1.0f;
	}
	return p;
}
Picture flat( int W, int H, double level )
{
	return flat( W, H, level, level, level );
}

void paint( Picture& p, int W, int H, int x0, int y0, int x1, int y1, double r, double g, double b )
{
	for( int y = std::max( 0, y0 ); y < std::min( H, y1 ); ++y )
		for( int x = std::max( 0, x0 ); x < std::min( W, x1 ); ++x )
		{
			float* px = p.data() + ( static_cast< size_t >( y ) * W + x ) * 4;
			px[ 0 ]   = static_cast< float >( r );
			px[ 1 ]   = static_cast< float >( g );
			px[ 2 ]   = static_cast< float >( b );
		}
}
void paint( Picture& p, int W, int H, int x0, int y0, int x1, int y1, double level )
{
	paint( p, W, H, x0, y0, x1, y1, level, level, level );
}

float at( const std::vector< float >& img, int W, int r, int c, int ch = 0 )
{
	return img[ ( static_cast< size_t >( r ) * W + c ) * 4 + ch ];
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	//INTEST_RENDERER=software asks for Apple's software renderer by id, on a
	//Mac that has a GPU. It is what a GPU-less CI runner falls back to, and it
	//is not bit-repeatable frame to frame (repousse's resize check failed CI
	//by one ulp), so a check that would fail only in CI can be run here first
	//(wipe's recipe, by way of repousse).
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "INTEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		std::fprintf( stderr, "intest: INTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}

	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;

	CGLSetCurrentContext( context );
	return context;
}

GLuint makeTexture( int width, int height, GLint internalFormat, GLenum type, const void* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, type, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

GLuint makeFramebuffer( GLuint texture )
{
	GLuint fbo = 0;
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	return fbo;
}

template< typename T >
std::vector< T > flipRows( const std::vector< T >& image, int width, int height )
{
	std::vector< T > flipped( image.size() );
	const size_t stride = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::copy( image.begin() + static_cast< long >( ( height - 1 - y ) * stride ),
		           image.begin() + static_cast< long >( ( height - y ) * stride ),
		           flipped.begin() + static_cast< long >( y * stride ) );
	return flipped;
}

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	unsigned int type;
	float value;
	float low;
	float high;
};

const char* kindName( const NamedParameter& p )
{
	if( p.index >= Instant::PT_ABOUT_FIRST )
		return "about";
	switch( p.type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_INTEGER: return "integer";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_STANDARD: return "standard";
	default: return "other";
	}
}

std::vector< NamedParameter > listParameters( Instant& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < Instant::PT_COUNT; ++i )
	{
		const char* const name = plugin.GetParamName( i );
		NamedParameter p;
		p.name  = name ? name : "?";
		p.index = i;
		p.type  = plugin.GetParamType( i );
		p.value = plugin.GetFloatParameter( i );
		p.low   = 0.0f;
		p.high  = 1.0f;
		//An option's range reads back 0..1 whatever its element count, so
		//the element count is the range; an integer's range is real.
		if( p.type == FF_TYPE_OPTION )
			p.high = static_cast< float >( std::max( 1u, plugin.GetNumParamElements( i ) ) - 1u );
		else if( p.type == FF_TYPE_INTEGER )
		{
			const RangeStruct range = plugin.GetParamRange( i );
			p.low                   = range.min;
			p.high                  = range.max;
		}
		list.push_back( p );
	}
	return list;
}

int indexOfParameter( Instant& plugin, const std::string& name )
{
	for( const NamedParameter& p : listParameters( plugin ) )
		if( p.name == name )
			return static_cast< int >( p.index );
	return -1;
}

bool applySetting( Instant& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.rfind( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name = assignment.substr( 0, equals );
	const int index        = indexOfParameter( plugin, name );
	if( index < 0 )
	{
		error = "no parameter called '" + name + "'";
		return false;
	}
	plugin.SetFloatParameter( static_cast< unsigned int >( index ), std::strtof( assignment.substr( equals + 1 ).c_str(), nullptr ) );
	return true;
}

bool set( Instant& plugin, const char* name, float value )
{
	std::string error;
	char buffer[ 64 ];
	std::snprintf( buffer, sizeof( buffer ), "%.9g", value );
	if( applySetting( plugin, std::string( name ) + "=" + buffer, error ) )
		return true;
	std::fprintf( stderr, "%s\n", error.c_str() );
	return false;
}

/// Every control a check can move, as the sliders the plugin sees. The
/// defaults here are the CLEAN print: colour film at 24 degC, fresh, in Take
/// mode (so nothing retakes behind a check's back), no exposure change, a
/// full spread, a clean roller, 64x, Border 0 (the image is the whole raster,
/// so film coordinates are pixel coordinates), no Mix. Each check moves the
/// one thing it measures.
struct Knobs
{
	int film        = 0;
	double celsius  = 24.0;
	float expired   = 0.0f;
	int mode        = controls::kModeTake;
	double interval = 16.0;
	float exposure  = 0.5f;
	int spread      = controls::kSpreadFull;
	float roller    = 0.0f;
	double speed    = 64.0;
	float border    = 0.0f;
	float mix       = 1.0f;
};

void apply( Instant& p, const Knobs& k )
{
	set( p, "Film", static_cast< float >( k.film ) );
	set( p, "Temperature", controls::TemperatureParam( k.celsius ) );
	set( p, "Expired", k.expired );
	set( p, "Mode", static_cast< float >( k.mode ) );
	set( p, "Interval", controls::IntervalParam( k.interval ) );
	set( p, "Exposure", k.exposure );
	set( p, "Spread", static_cast< float >( k.spread ) );
	set( p, "Dirty Roller", k.roller );
	set( p, "Speed", controls::SpeedParam( k.speed ) );
	set( p, "Border", k.border );
	set( p, "Mix", k.mix );
}

/// A Take press, as a host delivers one: 1.0 then 0.0.
void press( Instant& p )
{
	const int take = indexOfParameter( p, "Take" );
	p.SetFloatParameter( static_cast< unsigned int >( take ), 1.0f );
	p.SetFloatParameter( static_cast< unsigned int >( take ), 0.0f );
}

//---------------------------------------------------------------------------
// A session: the plugin, its input and output, and the clock that drives it.
//---------------------------------------------------------------------------
struct Session
{
	Instant plugin;
	int width  = 0;
	int height = 0;
	double fps = 60.0;
	/// Render into an RGBA32F framebuffer rather than RGBA8. The physics
	/// checks read float so their tolerances can be float-derived.
	bool floatOutput = true;

	GLuint sourceTexture = 0;
	GLuint outputTexture = 0;
	GLuint outputFBO     = 0;
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };
	ProcessOpenGLStruct process    = {};

	void makeTargets()
	{
		sourceTexture = makeTexture( width, height, GL_RGBA32F, GL_FLOAT, nullptr );
		outputTexture = floatOutput ? makeTexture( width, height, GL_RGBA32F, GL_FLOAT, nullptr )
		                            : makeTexture( width, height, GL_RGBA8, GL_UNSIGNED_BYTE, nullptr );
		outputFBO     = makeFramebuffer( outputTexture );

		inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
		inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
		inputStruct.Handle                              = sourceTexture;
		inputs[ 0 ]                                     = &inputStruct;

		process.numInputTextures = 1;
		process.inputTextures    = inputs;
		process.HostFBO          = outputFBO;
	}

	void dropTargets()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool begin( int w, int h )
	{
		width  = w;
		height = h;
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( width );
		viewport.height             = static_cast< FFUInt32 >( height );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see the diagnostics log for which shader\n" );
			return false;
		}
		makeTargets();
		return true;
	}

	/// What a host does when the clip or the composition changes size: hand
	/// the SAME instance a differently sized input. No DeInitGL.
	void resize( int w, int h )
	{
		dropTargets();
		width  = w;
		height = h;
		makeTargets();
	}

	bool renderAt( int frame )
	{
		//A synthetic clock, and it has to be synthetic: left to the wall
		//clock the harness renders a hundred frames in a few milliseconds
		//and nothing is ever in the window. The unit is declared, not
		//inferred.
		plugin.SetClockScaleForTest( 1.0 );
		plugin.SetTime( static_cast< double >( frame ) / fps );

		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		const bool ok = plugin.ProcessOpenGL( &process ) == FF_SUCCESS;
		if( !ok )
			std::fprintf( stderr, "ProcessOpenGL failed on frame %d\n", frame );
		return ok;
	}

	bool render( int frame, const std::vector< unsigned char >& pixels )
	{
		const std::vector< unsigned char > flipped = flipRows( pixels, width, height );
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
		return renderAt( frame );
	}

	bool render( int frame, const Picture& pixels )
	{
		const std::vector< float > flipped = flipRows( pixels, width, height );
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, flipped.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
		return renderAt( frame );
	}

	std::vector< unsigned char > readBack()
	{
		std::vector< unsigned char > pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		return flipRows( pixels, width, height );
	}

	std::vector< float > readBackFloat()
	{
		std::vector< float > pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return flipRows( pixels, width, height );
	}

	/// One pixel, float RGBA, row counted from the TOP like every picture here.
	std::vector< float > readPixel( int row, int col )
	{
		std::vector< float > px( 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( col, height - 1 - row, 1, 1, GL_RGBA, GL_FLOAT, px.data() );
		return px;
	}

	/// One column, float RGBA, top-first: index row * 4 + channel.
	std::vector< float > readColumn( int col )
	{
		std::vector< float > px( static_cast< size_t >( height ) * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( col, 0, 1, height, GL_RGBA, GL_FLOAT, px.data() );
		return flipRows( px, 1, height );
	}

	void end()
	{
		plugin.DeInitGL();
		dropTargets();
	}
};

const char* verdict( bool ok )
{
	return ok ? "ok" : "FAIL";
}

int report( bool ok, bool quiet, const char* format, ... ) __attribute__( ( format( printf, 3, 4 ) ) );
int report( bool ok, bool quiet, const char* format, ... )
{
	++g_checks;
	if( !ok )
		++g_failures;
	//Quiet is a negative control's run: its failures are the point, and the
	//summary line says so. --perturb runs the same thing verbosely.
	if( quiet )
		return ok ? 0 : 1;
	va_list args;
	va_start( args, format );
	std::printf( "   %-4s ", verdict( ok ) );
	std::vprintf( format, args );
	std::printf( "\n" );
	va_end( args );
	return ok ? 0 : 1;
}


//---------------------------------------------------------------------------
// Shared pieces of the checks.
//---------------------------------------------------------------------------

/// A flat patch, as sRGB code values (the plugin decodes them). 0.5 is
/// linear 0.214, log10 H = -0.67: the middle of the colour stock's curve.
constexpr double kPatch = 0.5;

/// Film-seconds per frame at `speed` on the 60 fps clock.
double filmPerFrame( double speed )
{
	return speed / 60.0;
}

/// The pod edge is the bottom of the image. With Border 0 the image is the
/// raster, so a pixel's distance from the pod edge in image heights is its
/// centre's, counted from the BOTTOM row.
double podDistance( int rowFromTop, int H )
{
	return ( static_cast< double >( H - 1 - rowFromTop ) + 0.5 ) / H;
}

/// How well a density read out of the Probe is known, for a sample taken
/// on frame n of a print. Every term is derived, not fitted:
///   - the dye dose is a sum of n float adds, each rounding by at most half
///     an ULP of the running sum: relative n kU of the dose A. Through
///     D = Dinf ( 1 - e^( -A / tau ) ) that moves D by at most
///     Dinf ( A / tau ) e^( -A / tau ) n kU <= Dinf n kU / e;
///   - exp is good to ( 3 + 2 |x| ) ULP (GLSL 4.10 s8.2), and 1 - e^-x takes
///     that as an absolute error of at most ( 3 + 2x ) 2 kU at x <= 1, and
///     less than 12 kU of Dinf over the range the checks sample;
///   - Balance, the asymptote, the depth and the product: four roundings.
/// So |dD| <= Dinf ( n / e + 16 ) kU, and this rounds that up to
/// Dinf ( n + 24 ) kU. Nothing here was read off this machine's output.
double densityError( int n, double dinf )
{
	return dinf * ( n + 24 ) * kU;
}

/// The same for the opacifier, O = O0 e^( -S / tau_op ), as a RELATIVE
/// error: exp's ( 3 + 2x ) ULP, the product with O0, and the stop dose's n
/// adds (relative n kU of S, so x n kU of O).
double opacifierRelError( int n, double x )
{
	return ( 8.0 + 4.0 * x + x * n ) * 2.0 * kU;
}

/// A straight line through ln y against t, with a bound on its slope from
/// the samples' own error bounds, and a check that every sample is on the
/// line within what its error and the line's allow.
struct LogFit
{
	double slope      = 0.0;
	double slopeBound = 0.0;///< |slope - true slope| <= this
	int used          = 0;
	double worst      = 0.0;///< the worst |residual| / its bound
};

LogFit fitLog( const std::vector< double >& t, const std::vector< double >& y, const std::vector< double >& err )
{
	std::vector< double > ts, zs, eps;
	for( size_t j = 0; j < t.size(); ++j )
	{
		//Only samples whose log is known to 1%: past that the first-order
		//bound on ln( y + e ) - ln( y ) is not a bound.
		if( y[ j ] <= 0.0 || err[ j ] >= 0.01 * y[ j ] )
			continue;
		ts.push_back( t[ j ] );
		zs.push_back( std::log( y[ j ] ) );
		eps.push_back( err[ j ] / ( y[ j ] - err[ j ] ) );
	}
	LogFit fit;
	fit.used = static_cast< int >( ts.size() );
	if( fit.used < 3 )
		return fit;
	const double N = static_cast< double >( fit.used );
	double tbar = 0.0, zbar = 0.0;
	for( int j = 0; j < fit.used; ++j )
	{
		tbar += ts[ j ] / N;
		zbar += zs[ j ] / N;
	}
	double sxx = 0.0, sxz = 0.0;
	for( int j = 0; j < fit.used; ++j )
	{
		sxx += ( ts[ j ] - tbar ) * ( ts[ j ] - tbar );
		sxz += ( ts[ j ] - tbar ) * ( zs[ j ] - zbar );
	}
	fit.slope = sxz / sxx;
	//The slope is a fixed linear combination of the logs, so an error of at
	//most eps_j in each moves it by at most sum | w_j | eps_j.
	for( int j = 0; j < fit.used; ++j )
		fit.slopeBound += std::fabs( ( ts[ j ] - tbar ) / sxx ) * eps[ j ];
	//And each fitted value is the hat matrix's row times the logs, so a
	//residual can be at most eps_j + sum_k | h_jk | eps_k if the law holds.
	for( int j = 0; j < fit.used; ++j )
	{
		double bound = eps[ j ];
		for( int k = 0; k < fit.used; ++k )
			bound += std::fabs( 1.0 / N + ( ts[ j ] - tbar ) * ( ts[ k ] - tbar ) / sxx ) * eps[ k ];
		const double r = zs[ j ] - ( zbar + fit.slope * ( ts[ j ] - tbar ) );
		fit.worst      = std::max( fit.worst, std::fabs( r ) / bound );
	}
	return fit;
}

/// A print on a flat patch, sampled at one pixel every `stride` frames while
/// `keep` says so: the film age, and the four probe channels (Dc, Dm, Dy, O).
struct Series
{
	std::vector< int > frame;
	std::vector< double > age;
	std::vector< double > d[ 4 ];
	double worstClock = 0.0;
};

bool sampleFlat( Series& out, const Knobs& k, int W, int H, int perturb, int row, int col, int stride, int last )
{
	Session s;
	apply( s.plugin, k );
	s.plugin.SetPerturbForTest( perturb );
	s.plugin.SetProbeForTest( model::kProbeDensities );
	press( s.plugin );//delivered before the first frame: the take is frame 0
	if( !s.begin( W, H ) )
		return false;
	const Picture pic = flat( W, H, kPatch );
	for( int f = 0; f <= last; ++f )
	{
		if( !s.render( f, pic ) )
			return false;
		const double age = s.plugin.AgeForTest();
		out.worstClock   = std::max( out.worstClock, std::fabs( age - f * filmPerFrame( k.speed ) ) );
		if( f == 0 || f % stride != 0 )
			continue;
		const std::vector< float > px = s.readPixel( row, col );
		out.frame.push_back( f );
		out.age.push_back( age );
		for( int c = 0; c < 4; ++c )
			out.d[ c ].push_back( px[ c ] );
	}
	s.end();
	return true;
}

/// Each dye layer's time constant, out of the series, WITHOUT the model's
/// asymptote: for D = Dinf ( 1 - e^( -( t - t0 ) / tau ) ) sampled every dt,
/// successive differences are Dinf e^( -( t_j - t0 ) / tau ) ( 1 - e^( -dt / tau ) ),
/// so ln( difference ) is a straight line in t of slope -1 / tau whatever
/// Dinf and t0 are. `until` is the last age the law is meant to hold at.
LogFit layerFit( const Series& s, int layer, double until, double& dinfSeen )
{
	dinfSeen = 0.0;
	for( double v : s.d[ layer ] )
		dinfSeen = std::max( dinfSeen, v );
	std::vector< double > t, y, e;
	for( size_t j = 0; j + 1 < s.age.size(); ++j )
	{
		if( s.age[ j + 1 ] > until )
			break;
		t.push_back( s.age[ j ] );
		y.push_back( s.d[ layer ][ j + 1 ] - s.d[ layer ][ j ] );
		e.push_back( densityError( s.frame[ j ], dinfSeen ) + densityError( s.frame[ j + 1 ], dinfSeen ) );
	}
	return fitLog( t, y, e );
}

/// The opacifier's time constant, out of ln O against t.
LogFit opacifierFit( const Series& s, double until, double tauGuess )
{
	std::vector< double > t, y, e;
	for( size_t j = 0; j < s.age.size(); ++j )
	{
		if( s.age[ j ] > until )
			break;
		t.push_back( s.age[ j ] );
		y.push_back( s.d[ 3 ][ j ] );
		e.push_back( s.d[ 3 ][ j ] * opacifierRelError( s.frame[ j ], s.age[ j ] / tauGuess ) );
	}
	return fitLog( t, y, e );
}

//---------------------------------------------------------------------------
// --develop
//---------------------------------------------------------------------------
int runDevelop( int W, int H, int perturb, bool quiet = false )
{
	if( !quiet )
		std::printf( "develop: a flat patch at sRGB %.2f, 24 degC, 64x, Probe densities at the centre, %dx%d\n", kPatch, W, H );
	int failures = 0;
	const int row = H / 2, col = W / 2;
	const double t0 = podDistance( row, H ) / model::kFrontSpeed;

	struct Case
	{
		int film;
		const char* name;
		int layers;
		const double* tau;
	};
	const double mono[ 3 ] = { model::kTauMono, model::kTauMono, model::kTauMono };
	const Case cases[] = { { 0, "Colour", 3, model::kTauColour }, { 1, "Black & White", 1, mono } };
	const char* layerName[ 3 ] = { "cyan", "magenta", "yellow" };

	for( const Case& c : cases )
	{
		Knobs k;
		k.film = c.film;
		//Every sample before the stop: development ends when the stop dose,
		//here age - t0 at 24 degC, reaches kStopDose.
		const double until = t0 + model::kStopDose - 2.0 * filmPerFrame( k.speed );
		const int last     = static_cast< int >( until / filmPerFrame( k.speed ) );
		Series s;
		if( !sampleFlat( s, k, W, H, perturb, row, col, 4, last ) )
			return failures + 1;
		failures += report( s.worstClock <= 1e-9 * last, quiet, "%-13s the film clock is frame x %g / 60 to %.1e s over %d frames", c.name, k.speed, s.worstClock, last );
		for( int L = 0; L < c.layers; ++L )
		{
			double dinf    = 0.0;
			const LogFit f = layerFit( s, L, until, dinf );
			const double tau    = -1.0 / f.slope;
			const double tauTol = f.slopeBound * tau * tau;
			failures += report( f.used >= 20 && std::fabs( tau - c.tau[ L ] ) <= tauTol, quiet,
			                    "%-13s %-7s tau %.4f s from %d differences, stated %.1f (tolerance %.1e s)", c.name, c.layers == 1 ? "image" : layerName[ L ], tau, f.used, c.tau[ L ], tauTol );
			failures += report( f.used >= 20 && f.worst <= 1.0, quiet, "%-13s %-7s first-order: every difference on the line, worst at %.2f of its bound", c.name,
			                    c.layers == 1 ? "image" : layerName[ L ], f.worst );
		}
		if( c.layers == 1 )
		{
			double worst = 0.0;
			for( size_t j = 0; j < s.age.size(); ++j )
				worst = std::max( { worst, std::fabs( s.d[ 1 ][ j ] - s.d[ 0 ][ j ] ), std::fabs( s.d[ 2 ][ j ] - s.d[ 0 ][ j ] ) } );
			failures += report( worst <= densityError( last, 2.0 ), quiet, "%-13s one image: the three channels agree to %.1e", c.name, worst );
		}
		const LogFit o        = opacifierFit( s, until, model::kTauOpacifier );
		const double tauOp    = -1.0 / o.slope;
		const double tauOpTol = o.slopeBound * tauOp * tauOp;
		failures += report( o.used >= 20 && std::fabs( tauOp - model::kTauOpacifier ) <= tauOpTol && o.worst <= 1.0, quiet,
		                    "%-13s opacifier tau %.4f s from %d samples, stated %.1f (tolerance %.1e s), on the line (%.2f of its bound)", c.name, tauOp, o.used,
		                    model::kTauOpacifier, tauOpTol, o.worst );
	}
	return failures;
}

//---------------------------------------------------------------------------
// --arrhenius
//---------------------------------------------------------------------------
int runArrhenius( int W, int H, int perturb, bool quiet = false )
{
	const double cold = 14.0, hot = 34.0;
	if( !quiet )
		std::printf( "arrhenius: the same patch at %.0f and %.0f degC, 64x, every tau fitted from the picture, %dx%d\n", cold, hot, W, H );
	const int row = H / 2, col = W / 2;
	const double t0 = podDistance( row, H ) / model::kFrontSpeed;
	const char* layerName[ 3 ] = { "cyan", "magenta", "yellow" };

	double tau[ 2 ][ 4 ], rel[ 2 ][ 4 ];
	int used[ 2 ][ 4 ];
	double worst[ 2 ][ 4 ];
	const double temps[ 2 ] = { cold, hot };
	for( int i = 0; i < 2; ++i )
	{
		Knobs k;
		k.celsius = temps[ i ];
		//The stop comes when the stop dose, kStop ( age - t0 ), reaches
		//kStopDose; the stated kStop, from the stated law.
		const double kStop = controls::ArrheniusFactor( temps[ i ], model::kStopActivation );
		const double until = t0 + model::kStopDose / kStop - 2.0 * filmPerFrame( k.speed );
		const int last     = static_cast< int >( until / filmPerFrame( k.speed ) );
		Series s;
		if( !sampleFlat( s, k, W, H, perturb, row, col, 2, last ) )
			return 1;
		for( int L = 0; L < 3; ++L )
		{
			double dinf    = 0.0;
			const LogFit f = layerFit( s, L, until, dinf );
			tau[ i ][ L ]  = -1.0 / f.slope;
			rel[ i ][ L ]  = f.slopeBound / std::fabs( f.slope );
			used[ i ][ L ] = f.used;
			worst[ i ][ L ] = f.worst;
		}
		const LogFit o = opacifierFit( s, until, model::kTauOpacifier / kStop );
		tau[ i ][ 3 ]  = -1.0 / o.slope;
		rel[ i ][ 3 ]  = o.slopeBound / std::fabs( o.slope );
		used[ i ][ 3 ] = o.used;
		worst[ i ][ 3 ] = o.worst;
	}

	int failures = 0;
	for( int L = 0; L < 4; ++L )
	{
		//Each dye layer at its own stated activation energy (the colour stock).
		const double ea    = L < 3 ? model::kLayerActivation[ L ] : model::kStopActivation;
		const double want  = std::exp( ea / model::kGasConstant * ( 1.0 / ( cold + 273.15 ) - 1.0 / ( hot + 273.15 ) ) );
		const double ratio = tau[ 0 ][ L ] / tau[ 1 ][ L ];
		//A ratio of two fitted taus, each known to rel: the ratio to the sum.
		const double tol = ratio * ( rel[ 0 ][ L ] + rel[ 1 ][ L ] ) * 1.0001;
		const char* name = L < 3 ? layerName[ L ] : "opacifier";
		failures += report( used[ 0 ][ L ] >= 20 && used[ 1 ][ L ] >= 20 && std::fabs( ratio - want ) <= tol, quiet,
		                    "%-9s tau %.3f s at %.0f degC, %.3f s at %.0f: ratio %.5f, Arrhenius at %.0f J/mol %.5f (tolerance %.1e)", name, tau[ 0 ][ L ], cold,
		                    tau[ 1 ][ L ], hot, ratio, ea, want, tol );
		failures += report( worst[ 0 ][ L ] <= 1.0 && worst[ 1 ][ L ] <= 1.0, quiet, "%-9s first-order at both temperatures (worst %.2f, %.2f of the bound)", name, worst[ 0 ][ L ], worst[ 1 ][ L ] );
	}
	return failures;
}

//---------------------------------------------------------------------------
// --order
//---------------------------------------------------------------------------

/// The print's dye balance at a pixel, out of the PICTURE: the channel
/// densities d = -log10( linear / White ). The paper texture multiplies every
/// channel alike and so shifts every d alike; max - min cancels it.
void pictureDensities( const std::vector< float >& px, double d[ 3 ] )
{
	for( int c = 0; c < 3; ++c )
		d[ c ] = -std::log10( std::max( srgbDecode( px[ c ] ), 1e-12 ) / model::kWhiteColour[ c ] );
}

int runOrder( int W, int H, int perturb, bool quiet = false )
{
	const double early = 240.0;//film-seconds: the stated early time
	const int speed    = 64;
	const int fEarly   = static_cast< int >( std::lround( early / filmPerFrame( speed ) ) );
	const int fEnd     = 600;//640 film-seconds, past the stop at 600 + t0
	//The pixel: near the image's bottom-left corner, where the shine is
	//e^-11 of its peak. Its bound goes into the tolerance anyway.
	const int col = std::max( 1, W / 10 );
	const int row = H - 1 - std::max( 1, H / 10 );
	if( !quiet )
		std::printf( "order: a neutral patch, 24 degC, 64x; the picture's dye balance at %.0f s and on to %.0f s, %dx%d\n", early, fEnd * filmPerFrame( speed ), W, H );

	Session s;
	Knobs k;
	apply( s.plugin, k );
	s.plugin.SetPerturbForTest( perturb );
	press( s.plugin );
	if( !s.begin( W, H ) )
		return 1;
	const Picture pic = flat( W, H, kPatch );

	//The shine at this pixel, from the print pass's stated band (Border 0:
	//the print is the raster, heights of H).
	const double px    = ( col + 0.5 ) / H, py = ( H - 1 - row + 0.5 ) / H;
	const double band  = ( px + py - 0.95 ) / 0.22;
	const double shine = 0.012 * std::exp( -band * band );

	double dEarly[ 3 ] = {}, spreadAt[ 64 ] = {};
	int samples = 0;
	int worstStep = -1;
	double worstRise = -1.0, tolAtWorst = 0.0;
	double previous = 1e9, final = 0.0, finalTol = 0.0;
	int rises = 0;
	for( int f = 0; f <= fEnd; ++f )
	{
		if( !s.render( f, pic ) )
			return 1;
		if( f < fEarly || ( f - fEarly ) % 8 != 0 )
			continue;
		const std::vector< float > p = s.readPixel( row, col );
		double d[ 3 ];
		pictureDensities( p, d );
		double lo = std::min( { srgbDecode( p[ 0 ] ), srgbDecode( p[ 1 ] ), srgbDecode( p[ 2 ] ) } );
		//Per channel: the probe's density bound at Dmax, the encode's pow
		//(~16 ULP relative) as density, and the shine as a relative error on
		//the darkest channel.
		const double tolD = densityError( f, 1.65 ) + 32.0 * kU / kLn10 + shine / std::max( lo, 1e-6 ) / kLn10;
		const double spread = std::max( { d[ 0 ], d[ 1 ], d[ 2 ] } ) - std::min( { d[ 0 ], d[ 1 ], d[ 2 ] } );
		if( f == fEarly )
			std::copy( d, d + 3, dEarly );
		else if( spread > previous + 2.0 * tolD )
		{
			++rises;
			if( spread - previous > worstRise )
			{
				worstRise  = spread - previous;
				worstStep  = f;
				tolAtWorst = 2.0 * tolD;
			}
		}
		if( samples < 64 )
			spreadAt[ samples ] = spread;
		++samples;
		previous = spread;
		final    = spread;
		//The opacifier left at the end is (O0 red - O0 green) e^( -S / tau_op ).
		finalTol = 2.0 * tolD + ( model::kOpacifier[ 0 ] - model::kOpacifier[ 1 ] ) * std::exp( -( fEnd * filmPerFrame( speed ) - 1.0 ) / model::kTauOpacifier );
	}
	s.end();

	int failures = 0;
	failures += report( dEarly[ 0 ] > dEarly[ 1 ] && dEarly[ 1 ] > dEarly[ 2 ], quiet,
	                    "at %.0f s the cyan layer leads: density R %.4f > G %.4f > B %.4f, a blue-cyan print", early, dEarly[ 0 ], dEarly[ 1 ], dEarly[ 2 ] );
	failures += report( spreadAt[ 0 ] > 0.1, quiet, "at %.0f s the balance is off by %.4f in density (more than 0.1)", early, spreadAt[ 0 ] );
	if( worstStep >= 0 && !quiet )
		std::printf( "   the balance rose by %.2e at frame %d (tolerance %.1e)\n", worstRise, worstStep, tolAtWorst );
	failures += report( rises == 0, quiet, "from %.0f s the balance moves only toward neutral: %d of %d steps rose", early, rises, samples - 1 );
	failures += report( final <= finalTol, quiet, "at %.0f s, past the stop, the print is neutral: %.2e (tolerance %.1e)", fEnd * filmPerFrame( speed ), final, finalTol );
	return failures;
}


//---------------------------------------------------------------------------
// --cast
//---------------------------------------------------------------------------

/// What temperature does to a neutral grey once development is over, out of
/// the PICTURE. The manufacturer's support page: below 13 degC a green tint,
/// above 28 degC a yellow/red one; at the reference the film is balanced.
/// Green is G above both R and B; warm is R above B; each by kCastMargin in
/// density, a tint a viewer sees on a neutral: 0.02 is 4.7% in reflectance,
/// about three 8-bit levels at mid grey. It is a statement of "visible", not
/// a number read off this machine's output.
constexpr double kCastMargin = 0.02;

int runCast( int W, int H, int perturb, bool quiet = false )
{
	const double speed = 256.0;
	//The pixel and its shine, as --order's: near the bottom-left corner.
	const int col = std::max( 1, W / 10 );
	const int row = H - 1 - std::max( 1, H / 10 );
	const double px    = ( col + 0.5 ) / H, py = ( H - 1 - row + 0.5 ) / H;
	const double band  = ( px + py - 0.95 ) / 0.22;
	const double shine = 0.012 * std::exp( -band * band );
	if( !quiet )
		std::printf( "cast: a neutral patch at sRGB %.2f, colour film, %gx, read from the picture after the stop and the opacifier, %dx%d\n", kPatch, speed, W, H );

	struct Case
	{
		double celsius;
		int kind;//-1 cold, 0 reference, 1 hot
	};
	const Case cases[] = { { 4.0, -1 }, { model::kCastFitC, -1 }, { 12.0, -1 }, { model::kReferenceC, 0 }, { 30.0, 1 }, { 34.0, 1 }, { 36.0, 1 } };
	int failures = 0;
	for( const Case& c : cases )
	{
		Session s;
		Knobs k;
		k.celsius = c.celsius;
		k.speed   = speed;
		apply( s.plugin, k );
		s.plugin.SetPerturbForTest( perturb );
		press( s.plugin );
		if( !s.begin( W, H ) )
			return failures + 1;
		//Past the stop at this temperature (the front arrives within a
		//film-second), then ten opacifier time constants, so what is left of
		//it, e^-10 of O0, is inside the tolerance below.
		const double kStop = controls::ArrheniusFactor( c.celsius, model::kStopActivation );
		const double until = 1.0 + ( model::kStopDose + 10.0 * model::kTauOpacifier ) / kStop;
		const int fEnd     = static_cast< int >( std::ceil( until / filmPerFrame( speed ) ) );
		const Picture pic  = flat( W, H, kPatch );
		for( int f = 0; f <= fEnd; ++f )
			if( !s.render( f, pic ) )
				return failures + 1;
		const std::vector< float > p = s.readPixel( row, col );
		s.end();
		double d[ 3 ];
		pictureDensities( p, d );
		const double lo = std::min( { srgbDecode( p[ 0 ] ), srgbDecode( p[ 1 ] ), srgbDecode( p[ 2 ] ) } );
		//Per channel, --order's: the density bound at Dmax, the encode's pow,
		//the shine on the darkest channel; and the opacifier's residue.
		const double tolD = densityError( fEnd, 1.65 ) + 32.0 * kU / kLn10 + shine / std::max( lo, 1e-6 ) / kLn10
		                    + model::kOpacifier[ 0 ] * std::exp( -10.0 );
		const int r8 = static_cast< int >( std::lround( p[ 0 ] * 255.0 ) ), g8 = static_cast< int >( std::lround( p[ 1 ] * 255.0 ) ),
		          b8 = static_cast< int >( std::lround( p[ 2 ] * 255.0 ) );
		if( c.kind < 0 )
		{
			const double overR = d[ 0 ] - d[ 1 ], overB = d[ 2 ] - d[ 1 ];
			failures += report( overR >= kCastMargin + 2.0 * tolD && overB >= kCastMargin + 2.0 * tolD, quiet,
			                    "%4.1f degC cold, green: G above R by %.4f and above B by %.4f in density (margin %.2f + %.1e); 8-bit (%d, %d, %d)", c.celsius,
			                    overR, overB, kCastMargin, 2.0 * tolD, r8, g8, b8 );
		}
		else if( c.kind > 0 )
		{
			const double overB = d[ 2 ] - d[ 0 ];
			failures += report( overB >= kCastMargin + 2.0 * tolD, quiet,
			                    "%4.1f degC hot, warm: R above B by %.4f in density (margin %.2f + %.1e), G - B %.4f; 8-bit (%d, %d, %d)", c.celsius, overB,
			                    kCastMargin, 2.0 * tolD, d[ 2 ] - d[ 1 ], r8, g8, b8 );
		}
		else
		{
			const double spread = std::max( { d[ 0 ], d[ 1 ], d[ 2 ] } ) - std::min( { d[ 0 ], d[ 1 ], d[ 2 ] } );
			failures += report( spread <= 2.0 * tolD, quiet, "%4.1f degC reference, neutral at completion: the balance is off by %.2e (tolerance %.1e); 8-bit (%d, %d, %d)",
			                    c.celsius, spread, 2.0 * tolD, r8, g8, b8 );
		}
	}
	return failures;
}

//---------------------------------------------------------------------------
// --front
//---------------------------------------------------------------------------
int runFront( int W, int H, int perturb, bool quiet = false )
{
	//1x, so a frame is 1/60 film-second and the front, at one image height
	//a second, takes 60 frames to cross. Read at frame 90, when every row
	//has been reached: the stop dose there is S = age - t0 at 24 degC, and
	//the opacifier O = O0 e^( -S / tau_op ) says what S is.
	const int last = 90;
	if( !quiet )
		std::printf( "front: a flat patch at 1x, 24 degC; when each row began, from its opacifier at frame %d, %dx%d\n", last, W, H );
	Session s;
	Knobs k;
	k.speed = 1.0;
	apply( s.plugin, k );
	s.plugin.SetPerturbForTest( perturb );
	s.plugin.SetProbeForTest( model::kProbeDensities );
	press( s.plugin );
	if( !s.begin( W, H ) )
		return 1;
	const Picture pic = flat( W, H, kPatch );
	for( int f = 0; f <= last; ++f )
		if( !s.render( f, pic ) )
			return 1;
	const double age               = s.plugin.AgeForTest();
	const std::vector< float > colm = s.readColumn( W / 2 );
	s.end();

	//tau_op times O's relative error, plus the stop dose's own adds (n kU of
	//at most `age`), plus the arrival's rounding in the shader (s / v and
	//AgeStart + DAge - t0: a few kU of the age).
	const double tol = model::kTauOpacifier * opacifierRelError( last, age / model::kTauOpacifier ) + ( last + 8 ) * kU * age;
	double worst = 0.0;
	int bad      = 0;
	double sx = 0, sy = 0, sxx = 0, sxy = 0;
	for( int r = 0; r < H; ++r )
	{
		const double O      = colm[ static_cast< size_t >( r ) * 4 + 3 ];
		const double S      = -model::kTauOpacifier * std::log( O / model::kOpacifier[ 0 ] );
		const double began  = age - S;
		const double dist   = podDistance( r, H );
		const double stated = dist / model::kFrontSpeed;
		worst               = std::max( worst, std::fabs( began - stated ) );
		if( std::fabs( began - stated ) > tol )
			++bad;
		sx += dist;
		sy += began;
		sxx += dist * dist;
		sxy += dist * began;
	}
	const double slope = ( H * sxy - sx * sy ) / ( H * sxx - sx * sx );
	int failures = 0;
	failures += report( bad == 0, quiet, "every row began developing at distance / front speed: worst %.2e s (tolerance %.1e s), %d rows", worst, tol, H );
	failures += report( std::fabs( 1.0 / slope - model::kFrontSpeed ) <= 4.0 * tol * model::kFrontSpeed * model::kFrontSpeed, quiet,
	                    "the front's speed out of the picture: %.6f image heights / s, stated %.1f", 1.0 / slope, model::kFrontSpeed );
	return failures;
}

//---------------------------------------------------------------------------
// --roller
//---------------------------------------------------------------------------
int rollerCase( int W, int H, int perturb, bool quiet, bool whole )
{
	const double C  = model::kRollerCircumference * H;
	const double w  = model::kRollerWidth * H;
	const double s0 = model::kRollerPhase * C;
	const int fEnd  = 600;//past the stop: every pixel has the same dye dose
	if( !quiet )
		std::printf( "  %s: %dx%d, circumference %.4f px, mark %.3f px wide\n", whole ? "whole-pixel" : "fractional", W, H, C, w );

	Session s;
	Knobs k;
	k.roller = 1.0f;
	apply( s.plugin, k );
	s.plugin.SetPerturbForTest( perturb );
	s.plugin.SetProbeForTest( model::kProbeDensities );
	press( s.plugin );
	if( !s.begin( W, H ) )
		return 1;
	const Picture pic = flat( W, H, kPatch );
	for( int f = 0; f <= fEnd; ++f )
		if( !s.render( f, pic ) )
			return 1;
	const std::vector< float > colm = s.readColumn( W / 2 );
	s.end();

	//Densities by distance from the pod edge, in pixels: index i is the
	//pixel whose centre is i + 0.5 up.
	std::vector< double > D( static_cast< size_t >( H ) );
	for( int r = 0; r < H; ++r )
		D[ static_cast< size_t >( H - 1 - r ) ] = colm[ static_cast< size_t >( r ) * 4 ];

	//Unmarked rows: more than a pixel clear of every stated mark.
	auto clear = [ & ]( int i ) {
		const double c = i + 0.5;
		for( int m = 0; s0 + m * C <= H + C; ++m )
			if( std::fabs( c - ( s0 + m * C ) ) < 0.5 * w + 1.5 )
				return false;
		return true;
	};
	double ref = 0.0, lo = 1e9, hi = -1e9;
	int nref = 0;
	for( int i = 0; i < H; ++i )
		if( clear( i ) )
		{
			ref += D[ static_cast< size_t >( i ) ];
			lo = std::min( lo, D[ static_cast< size_t >( i ) ] );
			hi = std::max( hi, D[ static_cast< size_t >( i ) ] );
			++nref;
		}
	ref /= std::max( nref, 1 );
	const double tolD = densityError( fEnd, ref );
	int failures      = 0;
	failures += report( nref > 0 && hi - lo <= 2.0 * tolD, quiet, "    between the marks the print is flat: %.2e across %d rows (tolerance %.1e)", hi - lo, nref, 2.0 * tolD );

	double deepest = 0.0;
	for( double v : D )
		deepest = std::max( deepest, ref - v );
	failures += report( deepest > 0.1 * ref, quiet, "    the marks are there: %.3f of the dye at the deepest", deepest / ref );

	if( whole )
	{
		//The pattern translates onto itself exactly one circumference on.
		const int Ci = static_cast< int >( std::lround( C ) );
		double worst = 0.0;
		int n        = 0;
		for( int i = 0; i + Ci < H; ++i, ++n )
			worst = std::max( worst, std::fabs( D[ static_cast< size_t >( i + Ci ) ] - D[ static_cast< size_t >( i ) ] ) );
		failures += report( worst <= 2.0 * tolD, quiet, "    D( s + %d px ) = D( s ) on all %d rows: worst %.2e (tolerance %.1e)", Ci, n, worst, 2.0 * tolD );
	}

	//Centroids, whole and fractional alike. A box of width w under exact
	//area coverage has its centroid off by at most 1 / ( 8 w ) px: each
	//partial edge pixel's first moment is off by cov ( 1 - cov ) / 2 <= 1/8,
	//and the two edges' errors have opposite signs. The reads add
	//sum | x - c | 2 tolD / mass.
	std::vector< double > centre;
	std::vector< double > stated;
	for( int m = 0;; ++m )
	{
		const double c = s0 + m * C;
		if( c + 0.5 * w + 1.0 > H )
			break;
		if( c - 0.5 * w - 1.0 < 0.0 )
			continue;
		double mass = 0.0, moment = 0.0, spreadErr = 0.0;
		for( int i = std::max( 0, static_cast< int >( c - 0.5 * C ) ); i < std::min( H, static_cast< int >( c + 0.5 * C ) ); ++i )
		{
			const double def = ref - D[ static_cast< size_t >( i ) ];
			mass += def;
			moment += ( i + 0.5 ) * def;
		}
		const double cen = moment / mass;
		for( int i = std::max( 0, static_cast< int >( c - 0.5 * C ) ); i < std::min( H, static_cast< int >( c + 0.5 * C ) ); ++i )
			spreadErr += std::fabs( i + 0.5 - cen ) * 2.0 * tolD;
		centre.push_back( cen );
		stated.push_back( c );
		const double tol = 1.0 / ( 8.0 * w ) + spreadErr / mass;
		failures += report( std::fabs( cen - c ) <= tol, quiet, "    mark %d at %.4f px from the pod edge, stated %.4f (tolerance %.3f)", m, cen, c, tol );
	}
	failures += report( centre.size() >= 2, quiet, "    %zu whole marks inside the image", centre.size() );
	for( size_t m = 1; m < centre.size(); ++m )
	{
		const double gap = centre[ m ] - centre[ m - 1 ];
		const double tol = 2.0 / ( 8.0 * w ) + 0.01;
		failures += report( std::fabs( gap - C ) <= tol, quiet, "    period %.4f px, circumference %.4f px (tolerance %.3f)", gap, C, tol );
	}
	return failures;
}

int runRoller( int W, int H, int perturb, bool quiet = false )
{
	//The circumference is 5/16 of the image height, so it is a whole number
	//of pixels exactly when the height is a multiple of 16. The whole-pixel
	//case runs at the multiple of 16 at or below H; the fractional one at H,
	//or H + 4 when H is itself a multiple of 16 (then 5/16 of it ends .25).
	const int Hw = H - H % 16;
	const int Hf = H % 16 != 0 ? H : H + 4;
	if( !quiet )
		std::printf( "roller: Dirty Roller 1, a flat patch, past the stop, the centre column\n" );
	int failures = 0;
	failures += rollerCase( W, Hw, perturb, quiet, true );
	failures += rollerCase( W, Hf, perturb, quiet, false );
	return failures;
}

//---------------------------------------------------------------------------
// --take
//---------------------------------------------------------------------------
int runTake( int W, int H, int perturb, bool quiet = false )
{
	const int fire = 10, last = 100, up = 40, down = 60, mid = 50;
	const int W2 = W * 3 / 2, H2 = H * 3 / 2;
	if( !quiet )
		std::printf( "take: Take mode, the take at frame %d; B changes the clip after it, C also resizes to %dx%d at %d and back at %d, %dx%d\n", fire, W2, H2, up, down, W, H );

	auto before = [ & ]( int w, int h ) {
		Picture p = flat( w, h, 0.3 );
		paint( p, w, h, w / 2, 0, w, h, 0.7 );
		return p;
	};
	auto after = [ & ]( int w, int h ) {
		Picture p = flat( w, h, 0.9 );
		paint( p, w, h, w / 2, 0, w, h, 0.1 );
		return p;
	};

	std::map< int, std::vector< float > > got[ 3 ];
	bool viewfinder = true;
	for( int run = 0; run < 3; ++run )
	{
		Session s;
		Knobs k;
		apply( s.plugin, k );
		s.plugin.SetPerturbForTest( perturb );
		s.plugin.SetProbeForTest( model::kProbeDensities );
		if( !s.begin( W, H ) )
			return 1;
		for( int f = 0; f <= last; ++f )
		{
			if( run == 2 && f == up )
				s.resize( W2, H2 );
			if( run == 2 && f == down )
				s.resize( W, H );
			if( f == fire )
				press( s.plugin );
			const Picture pic = run == 0 || f <= fire ? before( s.width, s.height ) : after( s.width, s.height );
			if( !s.render( f, pic ) )
				return 1;
			if( f < fire && run == 0 )
				viewfinder = viewfinder && s.readBackFloat() == pic;
			if( f == 20 || f == mid || f == last )
				got[ run ][ f ] = s.readBackFloat();
		}
		s.end();
	}

	int failures = 0;
	failures += report( viewfinder, quiet, "before the take the output is the clip, exactly (frames 0-%d)", fire - 1 );

	//Interior pixels of each half: clear of the raster's edge and the
	//patches' boundary by 4 px, which is more than the two resamples'
	//footprint.
	auto interior = [ & ]( int r, int c, int w, int h ) { return r >= 4 && r < h - 4 && c >= 4 && c < w - 4 && std::abs( c - w / 2 ) >= 4; };

	//A: the clip never changes. B: it changes after the take. The same
	//print, to the density bound.
	const double tolD = densityError( last, 1.65 );
	for( int f : { 20, mid, last } )
	{
		double worst = 0.0;
		for( int r = 0; r < H; ++r )
			for( int c = 0; c < W; ++c )
				for( int ch = 0; ch < 4; ++ch )
					worst = std::max( worst, static_cast< double >( std::fabs( at( got[ 0 ][ f ], W, r, c, ch ) - at( got[ 1 ][ f ], W, r, c, ch ) ) ) );
		failures += report( worst <= tolD, quiet, "frame %3d: the print is the captured frame's though the clip changed: worst %.2e (tolerance %.1e)", f, worst, tolD );
	}
	const float d20 = at( got[ 1 ][ 20 ], W, H / 2, W / 4 ), d50 = at( got[ 1 ][ mid ], W, H / 2, W / 4 ), d100 = at( got[ 1 ][ last ], W, H / 2, W / 4 );
	failures += report( d20 < d50 && d50 < d100, quiet, "it keeps developing: cyan density %.4f, %.4f, %.4f at frames 20, %d, %d", d20, d50, d100, mid, last );

	//C at frame 50 is at 1.5x. Its film coordinate for a pixel is not A's,
	//so compare against A interpolated in the row (the dose is linear in s
	//through the front's arrival; D's curvature over a row is below 1e-8).
	//The resample's bilinear weights are 8-bit sub-texel on the hardware
	//GL guarantees; a dose that differs by 1 / ( v H ) s per row is then
	//off by at most 2^-9 of that, which D sees as Dinf / tau_min of it.
	const double interp = std::ldexp( 1.0, -9 ) / ( model::kFrontSpeed * H ) * 1.65 / model::kTauColour[ 0 ] * 2.0;
	const double tolC   = tolD + interp + 1e-8;
	{
		double worst = 0.0;
		for( int r2 = 0; r2 < H2; ++r2 )
			for( int c2 = 0; c2 < W2; ++c2 )
			{
				const double ya = ( r2 + 0.5 ) * H / H2 - 0.5, xa = ( c2 + 0.5 ) * W / W2 - 0.5;
				const int r = static_cast< int >( std::floor( ya ) ), c = static_cast< int >( std::lround( xa ) );
				if( !interior( r, c, W, H ) || !interior( r + 1, c, W, H ) || !interior( r2, c2, W2, H2 ) || std::abs( c2 - W2 / 2 ) < 8 )
					continue;
				const double t = ya - r;
				for( int ch = 0; ch < 4; ++ch )
				{
					const double a = ( 1.0 - t ) * at( got[ 0 ][ mid ], W, r, c, ch ) + t * at( got[ 0 ][ mid ], W, r + 1, c, ch );
					worst          = std::max( worst, std::fabs( at( got[ 2 ][ mid ], W2, r2, c2, ch ) - a ) );
				}
			}
		failures += report( worst <= tolC, quiet, "frame %3d, resized to %dx%d mid-development: the same print, worst %.2e (tolerance %.1e)", mid, W2, H2, worst, tolC );
	}
	{
		double worst = 0.0;
		for( int r = 0; r < H; ++r )
			for( int c = 0; c < W; ++c )
				if( interior( r, c, W, H ) )
					for( int ch = 0; ch < 4; ++ch )
						worst = std::max( worst, static_cast< double >( std::fabs( at( got[ 2 ][ last ], W, r, c, ch ) - at( got[ 0 ][ last ], W, r, c, ch ) ) ) );
		failures += report( worst <= 2.0 * tolC, quiet, "frame %3d, resized back: the same print, worst %.2e (tolerance %.1e)", last, worst, 2.0 * tolC );
	}
	return failures;
}

//---------------------------------------------------------------------------
// --meter
//---------------------------------------------------------------------------

/// The stated curve, in double, for the one check that states a density.
double softplusD( double u )
{
	return std::max( u, 0.0 ) + std::log( 1.0 + std::exp( -model::kKnee * std::fabs( u ) ) ) / model::kKnee;
}
double coverageD( double logH, const model::Stock& st )
{
	const double toe = st.shoulder - st.latitude;
	return std::clamp( ( softplusD( logH - toe ) - softplusD( logH - toe - st.latitude ) ) / st.latitude, 0.0, 1.0 );
}

int runMeter( int W, int H, int perturb, bool quiet = false )
{
	//Three flat patches, past the stop at 24 degC, where D = Dinf exactly
	//(Balance cancels the incomplete yellow by construction). Two are inside
	//the meter's range and must print identically, at the stated density of
	//mid grey; the third needs more gain than the camera has.
	const double levels[ 3 ] = { 0.3, 0.7, 0.03 };
	const int fEnd          = 600;
	if( !quiet )
		std::printf( "meter: flat patches at sRGB %.2f, %.2f and %.2f, past the stop, %dx%d\n", levels[ 0 ], levels[ 1 ], levels[ 2 ], W, H );
	double D[ 3 ][ 3 ];
	for( int i = 0; i < 3; ++i )
	{
		Session s;
		Knobs k;
		apply( s.plugin, k );
		s.plugin.SetPerturbForTest( perturb );
		s.plugin.SetProbeForTest( model::kProbeDensities );
		press( s.plugin );
		if( !s.begin( W, H ) )
			return 1;
		const Picture pic = flat( W, H, levels[ i ] );
		for( int f = 0; f <= fEnd; ++f )
			if( !s.render( f, pic ) )
				return 1;
		const std::vector< float > px = s.readPixel( H / 2, W / 2 );
		s.end();
		for( int c = 0; c < 3; ++c )
			D[ i ][ c ] = px[ c ];
	}
	const model::Stock& st = model::kStocks[ 0 ];
	const double want      = st.dmin[ 0 ] + ( st.dmax[ 0 ] - st.dmin[ 0 ] ) * ( 1.0 - coverageD( std::log10( model::kMeterTarget ), st ) );
	//The density bound, plus the curve's own float evaluation: a log, two
	//softplus (an exp and a log each) and a division, each a few ULP of
	//numbers below 2, well under 64 kU of Dmax; the capture is RGBA16F, so a
	//patch is stored to 2^-11 relative, which the meter divides out exactly
	//(the patch is flat) and the gain carries into log10 H as at most
	//2^-11 / ln 10 -- through the curve's slope ( Dmax - Dmin ) / Latitude.
	const double tolEqual = 2.0 * densityError( fEnd, st.dmax[ 0 ] );
	const double tolWant  = densityError( fEnd, st.dmax[ 0 ] ) + 64.0 * kU * st.dmax[ 0 ];
	int failures          = 0;
	double worstEqual = 0.0, worstWant = 0.0;
	for( int c = 0; c < 3; ++c )
	{
		worstEqual = std::max( worstEqual, std::fabs( D[ 0 ][ c ] - D[ 1 ][ c ] ) );
		worstWant  = std::max( { worstWant, std::fabs( D[ 0 ][ c ] - want ), std::fabs( D[ 1 ][ c ] - want ) } );
	}
	failures += report( worstEqual <= tolEqual, quiet, "sRGB %.2f and %.2f print the same: density %.5f and %.5f, worst %.2e (tolerance %.1e)", levels[ 0 ], levels[ 1 ], D[ 0 ][ 0 ], D[ 1 ][ 0 ], worstEqual, tolEqual );
	failures += report( worstWant <= tolWant, quiet, "at the stated density of metered mid grey, %.5f: worst %.2e (tolerance %.1e)", want, worstWant, tolWant );
	const double linear = srgbDecode( levels[ 2 ] );
	const double gainMax = std::exp2( model::kMeterMaxStop );
	const double clamped = st.dmin[ 0 ] + ( st.dmax[ 0 ] - st.dmin[ 0 ] ) * ( 1.0 - coverageD( std::log10( linear * gainMax ), st ) );
	//Clamped, the gain no longer divides the half-float rounding out: the
	//stored patch is off by up to 2^-11 relative, which is 2^-11 / ln 10 in
	//log10 H, times the curve's steepest slope ( Dmax - Dmin ) / Latitude.
	const double tolClamp = tolWant + std::ldexp( 1.0, -11 ) / kLn10 * ( st.dmax[ 0 ] - st.dmin[ 0 ] ) / st.latitude;
	failures += report( linear * gainMax < model::kMeterTarget && std::fabs( D[ 2 ][ 0 ] - clamped ) <= tolClamp, quiet,
	                    "sRGB %.2f needs %.1f stops; the camera gives %.0f and it prints darker, %.5f (stated %.5f, tolerance %.1e)", levels[ 2 ],
	                    std::log2( model::kMeterTarget / linear ), model::kMeterMaxStop, D[ 2 ][ 0 ], clamped, tolClamp );
	return failures;
}

//---------------------------------------------------------------------------
// --negative
//---------------------------------------------------------------------------
int runNegative( int W, int H )
{
	std::printf( "negative controls: each perturbation of the plugin's model must FAIL its check, %dx%d\n", W, H );
	struct Control
	{
		int bits;
		const char* what;
		int ( *check )( int, int, int, bool );
		const char* name;
	};
	const Control controls[] = {
		{ model::kPerturbSingleTau, "one tau (magenta's) for every dye layer", runOrder, "--order" },
		{ model::kPerturbNoFront, "every point starts at t0 = 0", runFront, "--front" },
		{ model::kPerturbNoArrhenius, "temperature changes no rate", runArrhenius, "--arrhenius" },
		{ model::kPerturbTauCyan, "cyan's tau x 1.1 in the shader", runDevelop, "--develop" },
		{ model::kPerturbRollerPeriod, "the roller 2% larger than stated", runRoller, "--roller" },
		{ model::kPerturbResizeClears, "a resize that clears the print", runTake, "--take" },
		{ model::kPerturbLiveCapture, "a capture that follows the clip", runTake, "--take" },
		{ model::kPerturbNoMeter, "the camera's meter ignored", runMeter, "--meter" },
		{ model::kPerturbSharedActivation, "v0.1.0's one activation energy for every dye", runCast, "--cast" },
	};
	int failures = 0;
	for( const Control& c : controls )
	{
		const int before = g_failures;
		const int checks = g_checks;
		const int failed = c.check( W, H, c.bits, true );
		g_failures       = before;
		g_checks         = checks;
		failures += report( failed > 0, false, "%-42s -> %s fails (%d of its checks)", c.what, c.name, failed );
	}
	return failures;
}

//---------------------------------------------------------------------------
// --names
//---------------------------------------------------------------------------
int runNames()
{
	std::printf( "names: nothing the host will silently truncate; every name unique; no trademark\n" );
	Instant plugin;
	int failures = 0;
	std::set< std::string > seen;
	auto clean = []( std::string s ) {
		for( char& ch : s )
			ch = static_cast< char >( std::tolower( static_cast< unsigned char >( ch ) ) );
		return s.find( "polaroid" ) == std::string::npos && s.find( "instax" ) == std::string::npos;
	};
	for( const NamedParameter& p : listParameters( plugin ) )
	{
		if( p.index >= Instant::PT_ABOUT_FIRST )
			continue;
		failures += report( p.name.size() <= 16, false, "%-16s %2zu characters", p.name.c_str(), p.name.size() );
		failures += report( seen.insert( p.name ).second, false, "%-16s unique", p.name.c_str() );
		failures += report( clean( p.name ), false, "%-16s names no trademark", p.name.c_str() );
		if( p.type == FF_TYPE_OPTION )
			for( unsigned int e = 0; e < plugin.GetNumParamElements( p.index ); ++e )
			{
				const std::string element = plugin.GetParamElementName( p.index, e );
				failures += report( clean( element ), false, "%-16s option '%s' names no trademark", p.name.c_str(), element.c_str() );
			}
	}
	failures += report( std::string( "SW Instant" ).size() <= 16, false, "display name 'SW Instant' is %zu characters", std::string( "SW Instant" ).size() );
	return failures;
}

//---------------------------------------------------------------------------
// The moving card, for --out, the sweep, the bench and a default --pipe: a
// row of colour patches (red, skin, yellow, green, cyan, blue, sky, white),
// a grey ramp, a white disc on an orbit, a static black square, and a
// drifting bar.
//---------------------------------------------------------------------------
std::vector< unsigned char > buildCard( int width, int height, int64_t frame )
{
	std::vector< unsigned char > img( static_cast< size_t >( width ) * height * 4 );
	const double t  = static_cast< double >( frame ) / 60.0;
	const double cx = 0.72 + 0.14 * std::cos( 1.2 * t ), cy = 0.68 + 0.16 * std::sin( 1.2 * t );
	const double barX = std::fmod( 0.05 * t, 1.0 );
	const double patches[ 8 ][ 3 ] = {
		{ 0.80, 0.10, 0.10 }, { 0.88, 0.67, 0.55 }, { 0.90, 0.85, 0.15 }, { 0.15, 0.60, 0.20 },
		{ 0.20, 0.80, 0.85 }, { 0.15, 0.25, 0.85 }, { 0.53, 0.81, 0.92 }, { 0.95, 0.95, 0.95 },
	};
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double fx = ( x + 0.5 ) / width, fy = ( y + 0.5 ) / height;
			double r = 0.40, g = 0.40, b = 0.40;
			//A row of patches.
			if( fy > 0.06 && fy < 0.30 )
			{
				const int i = std::clamp( static_cast< int >( ( fx - 0.04 ) / 0.115 ), 0, 7 );
				if( fx > 0.04 && fx < 0.96 && std::fmod( fx - 0.04, 0.115 ) < 0.105 )
				{
					r = patches[ i ][ 0 ];
					g = patches[ i ][ 1 ];
					b = patches[ i ][ 2 ];
				}
			}
			//A grey ramp.
			if( fy > 0.36 && fy < 0.46 && fx > 0.04 && fx < 0.96 )
				r = g = b = ( fx - 0.04 ) / 0.92;
			//A static black square.
			if( fx > 0.08 && fx < 0.28 && fy > 0.56 && fy < 0.92 )
				r = g = b = 0.02;
			//A white disc on an orbit.
			const double dx = ( fx - cx ) * width, dy = ( fy - cy ) * height;
			if( dx * dx + dy * dy < ( height * 0.06 ) * ( height * 0.06 ) )
				r = g = b = 0.98;
			//A drifting bar, in blue.
			if( std::fabs( fx - barX ) < 0.012 && fy > 0.5 )
			{
				r = 0.2;
				g = 0.3;
				b = 0.9;
			}
			unsigned char* px = img.data() + ( static_cast< size_t >( y ) * width + x ) * 4;
			px[ 0 ]           = static_cast< unsigned char >( std::lround( 255.0 * r ) );
			px[ 1 ]           = static_cast< unsigned char >( std::lround( 255.0 * g ) );
			px[ 2 ]           = static_cast< unsigned char >( std::lround( 255.0 * b ) );
			px[ 3 ]           = 255;
		}
	return img;
}

//---------------------------------------------------------------------------
// --bench
//---------------------------------------------------------------------------
double benchAt( Instant& plugin, int width, int height, int frames, double fps, size_t& stateBytes )
{
	Session session;
	session.floatOutput = false;
	for( const NamedParameter& p : listParameters( plugin ) )
		if( p.index < Instant::PT_ABOUT_FIRST && p.type != FF_TYPE_BUFFER && p.type != FF_TYPE_EVENT )
			session.plugin.SetFloatParameter( p.index, p.value );
	session.fps = fps;
	if( !session.begin( width, height ) )
		return -1.0;

	//The card is uploaded once and not per frame, so the upload is not in
	//the figure: a host's frame is already on the GPU. The plugin's cost
	//does not depend on what the frame holds -- the develop and print
	//passes run every frame whatever the picture does, and the capture only
	//on a take -- so a still is an honest load. (Wetplate's bench had a 4K
	//upload per frame on the clock: 9 of 11 ms.)
	const std::vector< unsigned char > card = buildCard( width, height, 0 );
	const int warmup                        = 20;
	for( int frame = 0; frame < warmup; ++frame )
		session.render( frame, card );
	glFinish();

	//The best of three runs. This machine's GPU is shared with whatever
	//else is rendering, and a run that lost the GPU for a few milliseconds
	//measures the contention, not the plugin; the minimum is the one
	//nothing else interrupted.
	double best = 1e9;
	for( int run = 0; run < 3; ++run )
	{
		const auto start = std::chrono::steady_clock::now();
		for( int frame = 0; frame < frames; ++frame )
			session.renderAt( warmup + run * frames + frame );
		glFinish();
		const auto end       = std::chrono::steady_clock::now();
		const double seconds = std::chrono::duration< double >( end - start ).count();
		best                 = std::min( best, seconds * 1000.0 / static_cast< double >( frames ) );
	}
	stateBytes = session.plugin.StateBytesForTest();
	session.end();
	return best;
}

int runBench( Instant& plugin, int frames, double fps )
{
	struct Size
	{
		const char* name;
		int width, height;
	};
	const Size sizes[] = {
		{ "1280x720  ", 1280, 720 },
		{ "1920x1080 ", 1920, 1080 },
		{ "3840x2160 ", 3840, 2160 },
	};

	std::printf( "%d frames each, best of three runs, after a 20-frame warm-up, glFinish both sides, the card uploaded once.\n\n", frames );
	std::printf( "resolution     ms/frame   equivalent fps   %% of a 60fps frame   state held\n" );
	for( const Size& size : sizes )
	{
		size_t bytes    = 0;
		const double ms = benchAt( plugin, size.width, size.height, frames, fps, bytes );
		std::printf( "%s    %7.3f       %8.0f            %5.1f%%          %6.1f MB\n", size.name, ms, ms > 0.0 ? 1000.0 / ms : 0.0,
		             ms / 16.667 * 100.0, static_cast< double >( bytes ) / 1048576.0 );
	}
	std::printf( "\nState is three picture-sized buffers at 8 bytes a texel: the capture (RGBA16F)\n"
	             "and both halves of the development state (RGBA32F), colour textures only. The\n"
	             "SDK's FBO attaches a depth renderbuffer to each that the plugin never uses and\n"
	             "this does not count. Whatever the settings above were, they are what was\n"
	             "measured; --set measures another. A take costs one capture pass more.\n" );
	return 0;
}

//---------------------------------------------------------------------------
// --dump-shaders
//---------------------------------------------------------------------------
int dumpShaders( const std::string& dir )
{
	namespace shaders = instant::shaders;
	const std::pair< const char*, std::string > files[] = {
		{ "vertex.vert", shaders::Vertex() },     { "capture.frag", shaders::Capture() }, { "meter.frag", shaders::Meter() },
		{ "develop.frag", shaders::Develop() },
		{ "resample.frag", shaders::Resample() }, { "print.frag", shaders::Print() },
	};
	for( const auto& f : files )
	{
		std::ofstream out( dir + "/" + f.first );
		if( !out )
		{
			std::fprintf( stderr, "cannot write %s/%s\n", dir.c_str(), f.first );
			return 1;
		}
		out << f.second;
	}
	std::printf( "wrote %zu shaders to %s\n", sizeof( files ) / sizeof( files[ 0 ] ), dir.c_str() );
	return 0;
}

//---------------------------------------------------------------------------
// --pipe cue sheet: one 'frame Name Value' per line, the fleet's format.
//
// A STANDARD parameter ramps linearly between cues. An option, a boolean
// and an integer STEP: they hold the last cue at or before the frame,
// because there is nothing between Tintype and Ambrotype to ramp through.
// An event fires on its cue frame only: 1 on that frame, 0 on every other,
// which is what a host's button press looks like to the plugin.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}
	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );
		int frame = 0;
		if( !( in >> frame ) )
			continue;
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		const float value = std::strtof( words.back().c_str(), nullptr );
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

float valueAt( const Track& track, int frame, unsigned int type )
{
	if( track.empty() )
		return 0.0f;
	if( type == FF_TYPE_EVENT )
	{
		for( const auto& cue : track )
			if( cue.first == frame )
				return cue.second;
		return 0.0f;
	}
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 1; i < track.size(); ++i )
		if( frame <= track[ i ].first )
		{
			const auto& a = track[ i - 1 ];
			const auto& b = track[ i ];
			if( type != FF_TYPE_STANDARD )
				return frame == b.first ? b.second : a.second;
			const float span = static_cast< float >( b.first - a.first );
			const float t    = span > 0.0f ? static_cast< float >( frame - a.first ) / span : 1.0f;
			return a.second + ( b.second - a.second ) * t;
		}
	return track.back().second;
}

//---------------------------------------------------------------------------
void usage()
{
	std::printf(
		"intest -- render and measure the Instant print\n"
		"\n"
		"  --out PATH          render the moving card through the plugin (default instant.png)\n"
		"  --size WxH          raster (default 1280x720); --width N / --height N also accepted\n"
		"  --frames N          frames to render before reading back (default 90)\n"
		"  --fps N             synthetic frame rate driving the clock (default 60)\n"
		"  --source card|flat|white|black   what to feed (card moves); --level V for flat\n"
		"  --set \"Name=V\"      set a parameter by its display name (element index for options).\n"
		"                      Repeatable. \"Take=1\" is one press before the first frame.\n"
		"  --list              every parameter, its kind, default and range\n"
		"\n"
		"  checks that render, at --size:\n"
		"  --develop           each dye layer follows its first-order law with the stated tau\n"
		"  --order             cyan leads at the stated early time; the balance then goes to neutral\n"
		"  --arrhenius         tau at 14 and 34 degC has each layer's own Arrhenius ratio\n"
		"  --cast              a neutral grey after the stop: green cold, neutral at 24 degC, warm hot\n"
		"  --front             a point starts developing distance / front speed after the take\n"
		"  --roller            the dirty roller repeats at its circumference, whole and fractional\n"
		"  --take              the print develops from the captured frame; a resize keeps it\n"
		"  --meter             the camera meters: two levels print alike, a third past its range darker\n"
		"  --negative          every check above can fail\n"
		"  --perturb BITS      run the checks verbosely against a perturbed print (bits in Model.h)\n"
		"\n"
		"  checks that need no GL:\n"
		"  --names             nothing the host will silently truncate; no trademark\n"
		"\n"
		"  --bench             time ProcessOpenGL at 720p, 1080p and 4K, and the state held\n"
		"  --dump-shaders DIR  write the exact GLSL the plugin compiles\n"
		"  --pipe              raw RGBA frames on stdin, raw RGBA frames on stdout\n"
		"  --script PATH       parameter cues for --pipe: 'frame Name Value'\n"
		"  --help\n" );
}
} // namespace

int main( int argc, char** argv )
{
	std::string outPath = "instant.png";
	std::string scriptPath;
	std::string dumpDir;
	std::string source = "card";
	double level   = 0.5;
	int width      = 1280;
	int height     = 720;
	int frames     = 90;
	int failRender = -1;
	int perturb    = 0;
	double fps     = 60.0;
	bool wantList  = false;
	bool wantBench = false;
	bool wantPipe  = false;
	bool allowNoGL = false;
	std::vector< std::string > settings;
	std::vector< std::string > checks;

	const std::set< std::string > rendered = { "--develop", "--order", "--arrhenius", "--front", "--roller", "--take", "--meter", "--cast", "--negative" };
	const std::set< std::string > offline  = { "--names" };

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			usage();
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--dump-shaders" && hasNext )
			dumpDir = argv[ ++i ];
		else if( argument == "--source" && hasNext )
			source = argv[ ++i ];
		else if( argument == "--level" && hasNext )
			level = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--size" && hasNext )
		{
			const std::string size = argv[ ++i ];
			const size_t x         = size.find( 'x' );
			if( x == std::string::npos )
			{
				std::fprintf( stderr, "--size wants WxH\n" );
				return 2;
			}
			width  = std::atoi( size.substr( 0, x ).c_str() );
			height = std::atoi( size.substr( x + 1 ).c_str() );
		}
		else if( argument == "--width" && hasNext )
			width = std::atoi( argv[ ++i ] );
		else if( argument == "--height" && hasNext )
			height = std::atoi( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--perturb" && hasNext )
			perturb = std::atoi( argv[ ++i ] );
		else if( argument == "--fail-render-at" && hasNext )
			failRender = std::atoi( argv[ ++i ] );//test hook: verify.sh proves --pipe exits 1 on a failed render
		else if( argument == "--list" )
			wantList = true;
		else if( argument == "--bench" )
			wantBench = true;
		else if( argument == "--pipe" )
			wantPipe = true;
		else if( argument == "--allow-no-gl" )
			allowNoGL = true;
		else if( rendered.count( argument ) || offline.count( argument ) )
			checks.push_back( argument );
		else
		{
			std::fprintf( stderr, "unknown argument: %s\n", argument.c_str() );
			usage();
			return 2;
		}
	}

	if( width <= 0 || height <= 0 || frames <= 0 || fps <= 0.0 )
	{
		std::fprintf( stderr, "width, height, frames and fps must all be positive\n" );
		return 2;
	}

	if( !dumpDir.empty() )
		return dumpShaders( dumpDir );

	if( wantList )
	{
		//No GL needed: answered before a context is made, so it works in CI.
		Instant plugin;
		std::printf( "%3s  %-16s  %-9s  %-8s  %s\n", "id", "name", "kind", "default", "range" );
		for( const NamedParameter& p : listParameters( plugin ) )
			std::printf( "%3u  %-16s  %-9s  %.4f    [%g..%g]\n", p.index, p.name.c_str(), kindName( p ), p.value, p.low, p.high );
		return 0;
	}

	if( !checks.empty() )
	{
		bool needGL = false;
		for( const std::string& check : checks )
		{
			if( check == "--names" )
			{
				runNames();
				std::printf( "\n" );
			}
			else
				needGL = true;
		}

		if( needGL )
		{
			CGLContextObj context = createContext();
			if( context == nullptr && allowNoGL )
				std::printf( "   SKIP  could not create an OpenGL 4.1 core context, accelerated or software.\n"
				             "         The rendering checks and their negative controls were NOT run.\n" );
			else if( context == nullptr )
			{
				std::printf( "   FAIL  could not create an OpenGL 4.1 core context\n" );
				++g_failures;
			}
			else
			{
				for( const std::string& check : checks )
				{
					if( check == "--develop" )
						runDevelop( width, height, perturb );
					else if( check == "--order" )
						runOrder( width, height, perturb );
					else if( check == "--arrhenius" )
						runArrhenius( width, height, perturb );
					else if( check == "--front" )
						runFront( width, height, perturb );
					else if( check == "--roller" )
						runRoller( width, height, perturb );
					else if( check == "--take" )
						runTake( width, height, perturb );
					else if( check == "--meter" )
						runMeter( width, height, perturb );
					else if( check == "--cast" )
						runCast( width, height, perturb );
					else if( check == "--negative" )
						runNegative( width, height );
					else
						continue;
					std::printf( "\n" );
				}
				CGLSetCurrentContext( nullptr );
				CGLDestroyContext( context );
			}
		}
		std::printf( "%d checks, %d failed\n", g_checks, g_failures );
		return g_failures == 0 ? 0 : 1;
	}

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL context\n" );
		return 1;
	}
	auto finish = [ & ]( int result ) {
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return result;
	};

	Session session;
	session.floatOutput = false;
	session.fps         = fps;
	for( const std::string& setting : settings )
	{
		std::string error;
		if( applySetting( session.plugin, setting, error ) )
			continue;
		std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
		return finish( 2 );
	}
	session.plugin.SetPerturbForTest( perturb );

	if( wantBench )
		return finish( runBench( session.plugin, frames < 40 ? 60 : frames, fps ) );

	if( wantPipe )
	{
		//Everything but the video goes to stderr: one stray byte in stdout is
		//a torn frame for the rest of the reel.
		struct Automation
		{
			unsigned int index;
			unsigned int type;
			Track track;
		};
		std::vector< Automation > automation;
		if( !scriptPath.empty() )
		{
			std::string error;
			const std::map< std::string, Track > tracks = loadScript( scriptPath, error );
			if( !error.empty() )
			{
				std::fprintf( stderr, "%s\n", error.c_str() );
				return finish( 2 );
			}
			for( const auto& entry : tracks )
			{
				const int index = indexOfParameter( session.plugin, entry.first );
				if( index < 0 )
				{
					std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n", entry.first.c_str() );
					return finish( 2 );
				}
				automation.push_back( { static_cast< unsigned int >( index ), session.plugin.GetParamType( static_cast< unsigned int >( index ) ), entry.second } );
			}
		}

		//A closed stdout must be a failed write we can see, not a SIGPIPE
		//that kills the process with 141 before it can say so.
		std::signal( SIGPIPE, SIG_IGN );

		if( !session.begin( width, height ) )
			return finish( 1 );

		std::vector< unsigned char > frame( static_cast< size_t >( width ) * height * 4 );
		int status = 0;
		for( int index = 0;; ++index )
		{
			size_t got = 0;
			while( got < frame.size() )
			{
				const ssize_t n = read( STDIN_FILENO, frame.data() + got, frame.size() - got );
				if( n <= 0 )
					break;
				got += static_cast< size_t >( n );
			}
			//A partial frame is the end of the stream, never a frame.
			if( got < frame.size() )
			{
				if( got > 0 )
					std::fprintf( stderr, "partial frame at the end (%zu of %zu bytes, %dx%d): dropped\n", got, frame.size(), width, height );
				break;
			}

			//Through the plugin's own setter, so a cue moves what a slider
			//would, and an event is a press.
			for( const Automation& a : automation )
				session.plugin.SetFloatParameter( a.index, valueAt( a.track, index, a.type ) );

			const bool rendered = index != failRender && session.render( index, frame );
			if( !rendered )
			{
				std::fprintf( stderr, "render failed at frame %d\n", index );
				status = 1;
				break;
			}

			const std::vector< unsigned char > out = session.readBack();
			size_t written                         = 0;
			while( written < out.size() )
			{
				const ssize_t put = write( STDOUT_FILENO, out.data() + written, out.size() - written );
				if( put <= 0 )
					break;
				written += static_cast< size_t >( put );
			}
			//The reader has gone: rendering on into a closed pipe is work
			//nobody will see, and a short frame is worse than none.
			if( written < out.size() )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				status = 1;
				break;
			}
		}
		session.end();
		return finish( status );
	}

	if( !session.begin( width, height ) )
		return finish( 1 );
	for( int frame = 0; frame < frames; ++frame )
	{
		bool ok = false;
		if( source == "card" )
			ok = session.render( frame, buildCard( width, height, frame ) );
		else if( source == "flat" )
			ok = session.render( frame, flat( width, height, level ) );
		else if( source == "white" )
			ok = session.render( frame, flat( width, height, 1.0 ) );
		else if( source == "black" )
			ok = session.render( frame, flat( width, height, 0.0 ) );
		else
		{
			std::fprintf( stderr, "unknown --source %s\n", source.c_str() );
			return finish( 2 );
		}
		if( !ok )
			return finish( 1 );
	}

	const std::vector< unsigned char > image = session.readBack();
	session.end();
	if( !writePng( outPath, width, height, image ) )
	{
		std::fprintf( stderr, "could not write %s\n", outPath.c_str() );
		return finish( 1 );
	}
	std::printf( "wrote %s (%dx%d, %d frames)\n", outPath.c_str(), width, height, frames );
	return finish( 0 );
}
