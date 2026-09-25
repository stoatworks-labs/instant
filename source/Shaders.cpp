#include "Shaders.h"

#include "Model.h"

#include <cmath>
#include <cstdio>

namespace instant::shaders
{
namespace
{
const char* const kVersion = "#version 410 core\n";

//---------------------------------------------------------------------------
// The vertex shader every pass shares.
//---------------------------------------------------------------------------
const char* const kVertexBody = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

//---------------------------------------------------------------------------
// The model. A fragment, not a shader: no #version, no main. The develop and
// print passes are each assembled around this one string (after the
// constants block `constants()` writes from Model.h), so there is one spread
// and one curve in the plugin.
//---------------------------------------------------------------------------
const char* const kModel = R"(
uniform int Perturb;       //negative-control hooks; always 0 in the plugin
uniform int Spread;        //0 full, 1 short, 2 uneven
uniform float Expired;     //0..1
uniform vec2 CropScale;    //source uv = 0.5 + ( film uv - 0.5 ) * CropScale

//sRGB transfer, both ways, the IEC 61966-2-1 piecewise form.
float srgbDecode( float v )
{
	return v <= 0.04045 ? v / 12.92 : pow( ( v + 0.055 ) / 1.055, 2.4 );
}
vec3 srgbDecode( vec3 v )
{
	return vec3( srgbDecode( v.r ), srgbDecode( v.g ), srgbDecode( v.b ) );
}
float srgbEncode( float v )
{
	v = clamp( v, 0.0, 1.0 );
	return v <= 0.0031308 ? v * 12.92 : 1.055 * pow( v, 1.0 / 2.4 ) - 0.055;
}
vec3 srgbEncode( vec3 v )
{
	return vec3( srgbEncode( v.r ), srgbEncode( v.g ), srgbEncode( v.b ) );
}

//PCG output mix: exact in 32 bits, the same on every driver.
uint hashInt( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
float hashUnit( uint h )
{
	return float( h >> 8u ) * ( 1.0 / 16777216.0 );
}

//Value noise along a line, -1..1: one hashed value per cell, smoothstepped
//between. `x` in cells.
float lineNoise( float x, uint seed )
{
	float i  = floor( x );
	float t  = x - i;
	uint a   = seed * 65537u + 7u;
	float n0 = hashUnit( hashInt( a + uint( int( i ) + 4096 ) ) );
	float n1 = hashUnit( hashInt( a + uint( int( i ) + 4097 ) ) );
	return 2.0 * mix( n0, n1, t * t * ( 3.0 - 2.0 * t ) ) - 1.0;
}

//---------------------------------------------------------------------------
// The spread, in FILM coordinates: u across the image 0..1, s up from the
// pod edge in image heights. Everything here is a function of ( u, s ) and
// of the controls, so the develop pass (on the source raster) and the print
// pass (on the output raster) agree on it at any raster.
//---------------------------------------------------------------------------

//How far the reagent gets from the pod edge, in image heights.
float reachAt( float u )
{
	float w = 2.0 * u - 1.0;
	float r = kFullReach;
	if( Spread == 1 )
		r = kShortReach * ( 1.0 - kShortCorner * w * w );
	else if( Spread == 2 )
		r = 1.03 + 0.09 * lineNoise( u * 9.0, 11u ) - 0.06 * w * w;
	//Old paste has dried and thickened: it reaches less far, and least far
	//at the corners, where the frame's edge holds it back.
	r *= 1.0 - kExpiredReach * Expired * ( 0.55 + 0.45 * w * w );
	return r;
}

//The front's speed at u, image heights per film-second. Uneven paste runs
//in lanes.
float frontSpeedAt( float u )
{
	if( Spread == 2 )
		return kFrontSpeed * ( 1.0 + 0.25 * lineNoise( u * 5.0, 23u ) );
	return kFrontSpeed;
}

//When the front reaches ( u, s ), in film-seconds after the take.
float arrivalAt( float u, float s )
{
	if( ( Perturb & 2 ) != 0 )
		return 0.0;
	return max( s, 0.0 ) / frontSpeedAt( u );
}

//The reagent's depth, as a factor on the dye it can deliver: 1 on a full
//spread; lanes on an uneven one; thinning over the last few percent before
//the reach, where the tongue of paste runs out.
float depthAt( float u, float s, float reach )
{
	float d = 1.0;
	if( Spread == 2 )
		d -= 0.10 * ( 0.5 + 0.5 * lineNoise( u * 13.0, 37u ) );
	d *= 0.55 + 0.45 * smoothstep( reach, reach - 0.06, s );
	return d;
}

//---------------------------------------------------------------------------
// The curve. Coverage, as rebate's and wetplate's: 0 below the toe, 1 past
// the shoulder, a straight line of slope 1 / Latitude between.
//---------------------------------------------------------------------------
float softplus( float u )
{
	return max( u, 0.0 ) + log( 1.0 + exp( -kKnee * abs( u ) ) ) / kKnee;
}
float coverage( float logH, float Toe, float Latitude )
{
	return clamp( ( softplus( logH - Toe ) - softplus( logH - Toe - Latitude ) ) / Latitude, 0.0, 1.0 );
}
)";

//---------------------------------------------------------------------------
// Pass 1: capture. The output raster is the input raster, so texel ( x, y )
// is pixel ( x, y ): texelFetch, exact, whatever the host set the texture's
// filter to.
//---------------------------------------------------------------------------
const char* const kCaptureBody = R"(
uniform sampler2D InputTexture;

in vec2 uv;
out vec4 fragColor;

void main()
{
	vec4 c = texelFetch( InputTexture, ivec2( gl_FragCoord.xy ), 0 );
	//The film sees light, and a transparent pixel sent none.
	fragColor = vec4( srgbDecode( c.rgb ) * clamp( c.a, 0.0, 1.0 ), 1.0 );
}
)";

//---------------------------------------------------------------------------
// Pass 1b: meter. The camera's averaging cell, at the take: the capture's
// luminance over the image window, sampled onto a 64 x 64 grid whose mip
// chain averages it to one texel.
//---------------------------------------------------------------------------
const char* const kMeterBody = R"(
uniform sampler2D Capture;

in vec2 uv;
out vec4 fragColor;

void main()
{
	vec2 q = 0.5 + ( uv - 0.5 ) * CropScale;
	fragColor = vec4( dot( texture( Capture, q ).rgb, vec3( 0.2126, 0.7152, 0.0722 ) ), 0.0, 0.0, 1.0 );
}
)";

//---------------------------------------------------------------------------
// Pass 2: develop. One frame's worth of reagent time, per texel of the
// source raster.
//---------------------------------------------------------------------------
const char* const kDevelopBody = R"(
uniform sampler2D Previous;  //( cyan, magenta, yellow dye doses, stop dose ), same raster
uniform vec2 Size;           //the raster, pixels
uniform float AgeStart;      //film-seconds since the take, at this frame's start
uniform float DAge;          //film-seconds this frame
uniform vec3 KDye;           //each dye layer's Arrhenius rate, 1 at 24 degC
uniform float KStop;         //the timing layer's
uniform float StopDose;      //when development ends, stop-seconds

in vec2 uv;
out vec4 fragColor;

void main()
{
	ivec2 p   = ivec2( gl_FragCoord.xy );
	vec4 dose = texelFetch( Previous, p, 0 );

	//This texel's place on the film. The source raster is the whole clip
	//frame; the image window shows its centre, so film coordinates are the
	//inverse of the print pass's crop. Outside the crop the film goes on
	//(clamped), so a Border change mid-print never uncovers dry film.
	vec2 q = gl_FragCoord.xy / Size;
	vec2 f = 0.5 + ( q - 0.5 ) / CropScale;
	float u = clamp( f.x, 0.0, 1.0 );
	float s = max( f.y, 0.0 );

	if( s > reachAt( u ) )
	{
		fragColor = dose;
		return;
	}

	//The part of this frame after the front arrived. In steady state the
	//whole frame, exactly: the branch keeps a large AgeStart's rounding out
	//of it.
	float t0      = arrivalAt( u, s );
	float overlap = AgeStart >= t0 ? DAge : clamp( AgeStart + DAge - t0, 0.0, DAge );

	//The timing layer runs on; the dye moves only until the stop, and on
	//the frame that crosses it only for the part of the frame before it.
	//Each layer at its own rate.
	float dStop = KStop * overlap;
	float part  = dStop > 0.0 ? clamp( ( StopDose - dose.w ) / dStop, 0.0, 1.0 ) : 0.0;
	fragColor   = vec4( dose.xyz + KDye * overlap * part, dose.w + dStop );
}
)";

//---------------------------------------------------------------------------
// Pass 3: resample. One buffer into a buffer of another size.
//---------------------------------------------------------------------------
const char* const kResampleBody = R"(
uniform sampler2D Source;
uniform vec2 HalfTexel;  //half a SOURCE texel

in vec2 uv;
out vec4 fragColor;

void main()
{
	//Half a texel in from the edge so GL_LINEAR at the boundary does not
	//take half its weight from the clamp. On a flat field this is exact.
	vec2 q = clamp( uv, HalfTexel, vec2( 1.0 ) - HalfTexel );
	fragColor = texture( Source, q );
}
)";

//---------------------------------------------------------------------------
// Pass 4: the print, straight to the host. Pixel coordinates are GL's,
// y UP, so the pod edge (the bottom of the image) is ImageMin.y.
//---------------------------------------------------------------------------
const char* const kPrintBody = R"(
uniform sampler2D Dose;      //( cyan, magenta, yellow dye doses, stop dose )
uniform sampler2D Capture;   //linear light at the take
uniform sampler2D Source;    //the host's input, for Mix and the viewfinder
uniform sampler2D Meter;     //the meter's reading, at its top mip level
uniform float MeterLevel;
uniform vec2 MaxUV;
uniform vec2 PrintMin;       //pixels
uniform vec2 PrintMax;
uniform vec2 ImageMin;
uniform vec2 ImageMax;
uniform int HasPrint;        //0: Take mode before the first take -- the viewfinder
uniform int Mono;            //one image layer, exposed by luminance
uniform float ExposureGain;  //2 ^ Exposure stops, the wheel on top of the meter
uniform float Toe;
uniform float Latitude;
uniform vec3 Dmin;           //per layer C M Y, reflection density, Expired applied
uniform vec3 Dmax;
uniform vec3 Balance;        //makes a neutral neutral at the stop at 24 degC
uniform vec3 Tau;            //per layer, dye-seconds
uniform float TauOp;         //the opacifier's, stop-seconds
uniform vec3 White;          //the pigment, linear reflectance
uniform float Roller;        //0..1
uniform float MixAmount;
uniform int Probe;           //1: write ( Dc, Dm, Dy, O_red ) raw

in vec2 uv;
out vec4 fragColor;

//The fraction of the pixel [ c - 0.5, c + 0.5 ] inside [ a, b ]: exact
//area coverage along one axis.
float span( float c, float a, float b )
{
	return clamp( min( c + 0.5, b ) - max( c - 0.5, a ), 0.0, 1.0 );
}

//The dirty roller: one mark per revolution along the spread, each a band
//across the print of width kRollerWidth, with exact pixel coverage along
//the spread. `sigma` is the pixel centre's distance from the pod edge in
//pixels, `himg` the image height in pixels. Computed in coordinates local
//to the nearest mark, so that when the circumference is a whole number of
//pixels every mark is the same arithmetic on the same numbers.
float rollerMarks( float sigma, float himg )
{
	float C  = kRollerCircumference * himg * ( ( Perturb & 16 ) != 0 ? 1.02 : 1.0 );
	float s0 = kRollerPhase * C;
	float hw = 0.5 * kRollerWidth * himg;
	float k  = floor( ( sigma - s0 ) / C + 0.5 );
	float m  = 0.0;
	for( int j = -1; j <= 1; ++j )
	{
		float kj = k + float( j );
		float c  = s0 + kj * C;
		if( kj < 0.0 || c > himg )
			continue;
		float local = ( sigma - s0 ) - kj * C;//the pixel centre, from this mark's centre
		m += span( local, -hw, hw );
	}
	return m;
}

void main()
{
	vec2 px = gl_FragCoord.xy;
	vec4 source = texture( Source, uv * MaxUV );
	if( HasPrint == 0 )
	{
		fragColor = source;
		return;
	}

	float printCov = span( px.x, PrintMin.x, PrintMax.x ) * span( px.y, PrintMin.y, PrintMax.y );
	float imageCov = span( px.x, ImageMin.x, ImageMax.x ) * span( px.y, ImageMin.y, ImageMax.y );
	vec2 isize     = ImageMax - ImageMin;
	vec2 f         = ( px - ImageMin ) / isize;
	vec2 q         = 0.5 + ( f - 0.5 ) * CropScale;
	float u        = clamp( f.x, 0.0, 1.0 );
	float s        = max( f.y, 0.0 );

	//The camera's exposure: the meter brings the window's mean to mid grey,
	//within its range; the wheel offsets it.
	float mean = textureLod( Meter, vec2( 0.5 ), MeterLevel ).r;
	float gain = clamp( kMeterTarget / max( mean, 1e-9 ), kMeterMinGain, kMeterMaxGain );
	if( ( Perturb & 128 ) != 0 )
		gain = 1.0;

	//The layers' asymptotic dye, from the light at the take.
	vec3 light = texture( Capture, q ).rgb * gain * ExposureGain;
	if( Mono != 0 )
		light = vec3( dot( light, vec3( 0.2126, 0.7152, 0.0722 ) ) );
	vec3 logH = log( max( light, vec3( 1e-12 ) ) ) / log( 10.0 );
	vec3 dinf = vec3( coverage( logH.r, Toe, Latitude ), coverage( logH.g, Toe, Latitude ), coverage( logH.b, Toe, Latitude ) );
	dinf      = Dmin + ( Dmax - Dmin ) * ( 1.0 - dinf );

	//The spread: what reached here, how deep, and the roller's marks.
	float reach   = reachAt( u );
	float reached = 1.0 - smoothstep( reach - 0.004, reach + 0.004, s );
	float depth   = depthAt( u, s, reach );
	float marks   = rollerMarks( px.y - ImageMin.y, isize.y );
	float across  = 0.55 + 0.45 * lineNoise( u * 7.0, 53u );//the dirt is not even along the roller
	depth *= 1.0 - Roller * kRollerDepth * across * marks;

	//Development so far.
	vec4 dose = texture( Dose, q );
	vec3 tau  = Tau;
	if( ( Perturb & 1 ) != 0 )
		tau = vec3( Tau.g );
	if( ( Perturb & 8 ) != 0 )
		tau.r *= 1.1;
	vec3 D = Balance * dinf * depth * ( 1.0 - exp( -dose.xyz / tau ) );
	vec3 O = vec3( kOpacifierR, kOpacifierG, kOpacifierB ) * exp( -dose.w / TauOp );

	if( Probe == 1 )
	{
		fragColor = vec4( D, O.r );
		return;
	}

	//Seen by reflection off the white pigment, through the dye and the
	//opacifier; where no reagent reached, the dark negative.
	vec3 image = White * exp2( -( D + O ) * 3.321928094887362 );
	image      = mix( vec3( kUnreachedR, kUnreachedG, kUnreachedB ), image, reached );

	//The frame, with a faint shadow along the window's edge.
	vec2 pmm   = ( px - PrintMin ) / max( PrintMax.y - PrintMin.y, 1.0 );//print heights
	vec3 frame = vec3( kFrameR, kFrameG, kFrameB );
	vec2 fromWindow = max( ImageMin - px, px - ImageMax );
	float edge = max( fromWindow.x, fromWindow.y ) / max( isize.y, 1.0 );
	frame *= 1.0 - 0.10 * ( 1.0 - smoothstep( 0.0, 0.008, edge ) );

	//Texture: the matte frame more than the glossy image. Value noise at a
	//fixed size on the print, so it does not scale with the raster.
	float grainX = lineNoise( pmm.x * 900.0 + 17.0 * floor( pmm.y * 900.0 ), 71u );
	frame *= 1.0 + 0.030 * grainX;
	image *= 1.0 + 0.006 * grainX;

	vec3 col = mix( frame, image, imageCov );

	//A small shine: a soft diagonal band of the room's light off the gloss.
	float band = ( pmm.x + pmm.y - 0.95 ) / 0.22;
	col += 0.012 * exp( -band * band );

	//Outside the print there is nothing: colour AND alpha 0, so the host
	//sees transparency whether it reads the output as straight or
	//premultiplied. The antialiased edge is premultiplied for the same reason.
	float a    = printCov;
	vec3 print = srgbEncode( col ) * a;
	if( MixAmount >= 1.0 )
	{
		fragColor = vec4( print, a );
		return;
	}
	fragColor = vec4( mix( source.rgb, print, MixAmount ), mix( source.a, a, MixAmount ) );
}
)";

/// Model.h's numbers as GLSL constants, so there is one copy of each.
std::string constants()
{
	namespace m = instant::model;
	std::string s;
	char line[ 128 ];
	auto put = [ & ]( const char* name, double v ) {
		std::snprintf( line, sizeof( line ), "const float %s = %.9g;\n", name, v );
		s += line;
	};
	put( "kFullReach", m::kFullReach );
	put( "kShortReach", m::kShortReach );
	put( "kShortCorner", m::kShortCorner );
	put( "kExpiredReach", m::kExpiredReach );
	put( "kFrontSpeed", m::kFrontSpeed );
	put( "kKnee", m::kKnee );
	put( "kRollerCircumference", m::kRollerCircumference );
	put( "kRollerPhase", m::kRollerPhase );
	put( "kRollerWidth", m::kRollerWidth );
	put( "kRollerDepth", m::kRollerDepth );
	put( "kOpacifierR", m::kOpacifier[ 0 ] );
	put( "kOpacifierG", m::kOpacifier[ 1 ] );
	put( "kOpacifierB", m::kOpacifier[ 2 ] );
	put( "kUnreachedR", m::kUnreached[ 0 ] );
	put( "kUnreachedG", m::kUnreached[ 1 ] );
	put( "kUnreachedB", m::kUnreached[ 2 ] );
	put( "kFrameR", m::kFrameColour[ 0 ] );
	put( "kFrameG", m::kFrameColour[ 1 ] );
	put( "kFrameB", m::kFrameColour[ 2 ] );
	put( "kMeterTarget", m::kMeterTarget );
	put( "kMeterMinGain", std::exp2( m::kMeterMinStop ) );
	put( "kMeterMaxGain", std::exp2( m::kMeterMaxStop ) );
	return s;
}

std::string assemble( const char* body, bool withModel )
{
	std::string s = kVersion;
	if( withModel )
	{
		s += constants();
		s += kModel;
	}
	s += body;
	return s;
}
} // namespace

std::string Vertex()
{
	return assemble( kVertexBody, false );
}
std::string Capture()
{
	//The capture needs the sRGB decode from the model; one library is worth
	//more than a second copy of the transfer function.
	return assemble( kCaptureBody, true );
}
std::string Meter()
{
	return assemble( kMeterBody, true );
}
std::string Develop()
{
	return assemble( kDevelopBody, true );
}
std::string Resample()
{
	return assemble( kResampleBody, false );
}
std::string Print()
{
	return assemble( kPrintBody, true );
}

} // namespace instant::shaders
