#include "Controls.h"

#include "Model.h"

#include <algorithm>
#include <cmath>

namespace instant::controls
{
namespace
{
float clamp01( float v )
{
	return std::clamp( v, 0.0f, 1.0f );
}
} // namespace

double TemperatureC( float value )
{
	return 4.0 + 32.0 * static_cast< double >( clamp01( value ) );
}

float TemperatureParam( double celsius )
{
	return clamp01( static_cast< float >( ( celsius - 4.0 ) / 32.0 ) );
}

float Expired( float value )
{
	return clamp01( value );
}

double IntervalSeconds( float value )
{
	return std::exp2( 8.0 * static_cast< double >( clamp01( value ) ) - 1.0 );
}

float IntervalParam( double seconds )
{
	return clamp01( static_cast< float >( ( std::log2( std::max( seconds, 0.5 ) ) + 1.0 ) / 8.0 ) );
}

float ExposureStops( float value )
{
	return clamp01( value ) * 4.0f - 2.0f;
}

float RollerStrength( float value )
{
	return clamp01( value );
}

double SpeedFactor( float value )
{
	return std::exp2( 8.0 * static_cast< double >( clamp01( value ) ) );
}

float SpeedParam( double factor )
{
	return clamp01( static_cast< float >( std::log2( std::max( factor, 1.0 ) ) / 8.0 ) );
}

float Border( float value )
{
	return clamp01( value );
}

double ArrheniusFactor( double celsius, double activation )
{
	const double t    = celsius + 273.15;
	const double tRef = model::kReferenceC + 273.15;
	return std::exp( -activation / model::kGasConstant * ( 1.0 / t - 1.0 / tRef ) );
}

const char* FilmName( int index )
{
	static const char* const names[ kFilmCount ] = { "Colour", "Black & White", "Vintage" };
	return names[ std::clamp( index, 0, kFilmCount - 1 ) ];
}

const char* ModeName( int index )
{
	static const char* const names[ kModeCount ] = { "Take", "Continuous" };
	return names[ std::clamp( index, 0, kModeCount - 1 ) ];
}

const char* SpreadName( int index )
{
	static const char* const names[ kSpreadCount ] = { "Full", "Short", "Uneven" };
	return names[ std::clamp( index, 0, kSpreadCount - 1 ) ];
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

Layout PrintLayout( int width, int height, float border )
{
	const double b = static_cast< double >( Border( border ) );
	const double W = width, H = height;

	//The print at Border 1: 88 x 107 mm, fitted to 92% of the raster.
	const double printW = 88.0, printH = 107.0;
	const double scale  = 0.92 * std::min( W / printW, H / printH );//px per mm
	const double pw = printW * scale, ph = printH * scale;
	const double px0 = 0.5 * ( W - pw ), py0 = 0.5 * ( H - ph );
	//The image window: 4.5 mm in at the sides, 22 mm up from the bottom
	//(the pod's border), 6 mm down from the top; 79 x 79 mm.
	const double ix0 = px0 + 4.5 * scale, iy0 = py0 + 22.0 * scale;
	const double ix1 = ix0 + 79.0 * scale, iy1 = iy0 + 79.0 * scale;

	auto lerp = []( double a, double c, double t ) { return static_cast< float >( a + ( c - a ) * t ); };
	Layout l;
	l.printMin[ 0 ] = lerp( 0.0, px0, b );
	l.printMin[ 1 ] = lerp( 0.0, py0, b );
	l.printMax[ 0 ] = lerp( W, px0 + pw, b );
	l.printMax[ 1 ] = lerp( H, py0 + ph, b );
	l.imageMin[ 0 ] = lerp( 0.0, ix0, b );
	l.imageMin[ 1 ] = lerp( 0.0, iy0, b );
	l.imageMax[ 0 ] = lerp( W, ix1, b );
	l.imageMax[ 1 ] = lerp( H, iy1, b );
	return l;
}

} // namespace instant::controls
