#pragma once

/**
	Host parameters are 0..1; these are what they mean.

	`CFFGLPluginManager::SetParamInfo` clamps a STANDARD default into 0..1
	*before* returning, and `SetParamRange` can only be called afterwards, so
	a parameter declared in degrees cannot declare a default in degrees. Every
	slider here is therefore a plain 0..1 float and the conversions live in
	this one file, which the plugin and the harness both use.

	Every position a check needs to hit lands EXACTLY in binary: Temperature
	0.625 is 24 degC (the reference), 0.3125 is 14 and 0.9375 is 34; Speed at
	a multiple of 1/8 is a power of two (0 is 1x, honest; 0.75 is 64x);
	Interval at a multiple of 1/8 is a power of two of seconds; Exposure 0.5
	is 0 stops. A float round-trip that lands a hair off puts a take a frame
	late (wetplate measured it).

	Options are mapped by INDEX. An option parameter's range reads back 0..1
	from the SDK whatever its element count, so nothing outside this file
	should reason from the range.
*/
namespace instant::controls
{

/// Temperature: 4 to 36 degC, linear. 0.625 is exactly 24 degC.
double TemperatureC( float value );
float TemperatureParam( double celsius );

/// Expired: 0 fresh to 1 long past its date, linear.
float Expired( float value );

/// Interval: host seconds between prints in Continuous mode, 0.5 to 128,
/// geometric. 0.625 is exactly 16 s.
double IntervalSeconds( float value );
float IntervalParam( double seconds );

/// Exposure: -2 to +2 stops, linear. 0.5 is exactly 0.
float ExposureStops( float value );

/// Dirty Roller: the fraction of `kRollerDepth` a mark takes, 0 to 1.
float RollerStrength( float value );

/// Speed: film seconds per host second, 1 to 256, geometric. 0 is 1x --
/// a real print, which takes ten minutes; 0.75 is exactly 64x.
double SpeedFactor( float value );
float SpeedParam( double factor );

/// Border: 0 is the image full-frame with no border; 1 is the print, its
/// square image in its frame, thicker at the bottom where the pod was.
float Border( float value );

/// The Arrhenius factor on a rate at `celsius`, relative to 24 degC, for an
/// activation energy in J / mol. exp( -Ea / R ( 1 / T - 1 / Tref ) ).
double ArrheniusFactor( double celsius, double activation );

/// Option counts, and names in their menu order.
constexpr int kFilmCount = 3;
const char* FilmName( int index );
constexpr int kModeCount = 2;
const char* ModeName( int index );
constexpr int kModeTake       = 0;
constexpr int kModeContinuous = 1;
constexpr int kSpreadCount = 3;
const char* SpreadName( int index );
constexpr int kSpreadFull   = 0;
constexpr int kSpreadShort  = 1;
constexpr int kSpreadUneven = 2;

/// An option's value to its index, rounded and clamped.
int OptionIndex( float value, int count );

/// The print's layout in the output raster, in pixels, y UP (GL's frame).
/// At Border 0 both rectangles are the whole raster. At Border 1 the print
/// is 88 x 107 mm, 92% of the shorter fit, centred, and the image window
/// is 79 x 79 mm inside it: 4.5 mm at the sides, 6 mm at the top, 22 mm at
/// the bottom. Between, every edge moves linearly.
struct Layout
{
	float printMin[ 2 ], printMax[ 2 ];
	float imageMin[ 2 ], imageMax[ 2 ];
};
Layout PrintLayout( int width, int height, float border );

} // namespace instant::controls
