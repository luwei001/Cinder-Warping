/*
 Copyright (c) 2010-2013, Paul Houx - All rights reserved.
 This code is intended for use with the Cinder C++ library: http://libcinder.org

 This file is part of Cinder-Warping.

 Cinder-Warping is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Cinder-Warping is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Cinder-Warping.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Warp.h"
#include "WarpBilinear.h"
#include "WarpPerspective.h"
#include "WarpPerspectiveBilinear.h"

#include "cinder/Xml.h"
#include "cinder/app/App.h"
#include "cinder/gl/Texture.h"

using namespace ci;
using namespace ci::app;

namespace ph {
namespace warping {

bool	Warp::sIsEditMode = false;
double	Warp::sSelectedTime = 0.0;
ivec2	Warp::sMouse = ivec2( 0, 0 );

Warp::Warp( WarpType type ) :
mType( type ),
mIsDirty( true ),
mWidth( 640 ),
mHeight( 480 ),
mBrightness( 1.0f ),
mSelected( -1 ), // since this is an unsigned int, actual value will be 'MAX_INTEGER'
mControlsX( 2 ),
mControlsY( 2 )
{
	mWindowSize = vec2( float( mWidth ), float( mHeight ) );
}

Warp::~Warp( void )
{
}

void Warp::draw( const gl::Texture2dRef &texture )
{	
	// from https://github.com/jhurlbut/Cinder-Warping
	// draw( texture, texture->getBounds(), Rectf( getBounds() ) );
	if (mSrcArea.x1 > 0)
		draw(texture, mSrcArea, Rectf(getBounds()));
	else
		draw(texture, texture->getBounds(), Rectf(getBounds()));
}

void Warp::draw( const gl::Texture2dRef &texture, const Area &srcArea )
{
	draw( texture, srcArea, Rectf( getBounds() ) );
}
// from https://github.com/jhurlbut/Cinder-Warping
void Warp::setSrcArea(const ci::Area &srcArea) {
	mSrcArea = Area(srcArea);

}
bool Warp::clip( Area &srcArea, Rectf &destRect ) const
{
	bool clipped = false;

	float x1 = destRect.x1 / mWidth;
	float x2 = destRect.x2 / mWidth;
	float y1 = destRect.y1 / mHeight;
	float y2 = destRect.y2 / mHeight;

	if( x1 < 0.0f ) {
		destRect.x1 = 0.0f;
		srcArea.x1 -= static_cast<int32_t>( x1 * srcArea.getWidth() );
		clipped = true;
	}
	else if( x1 > 1.0f ) {
		destRect.x1 = static_cast<float>( mWidth );
		srcArea.x1 -= static_cast<int32_t>( ( 1.0f / x1 ) * srcArea.getWidth() );
		clipped = true;
	}

	if( x2 < 0.0f ) {
		destRect.x2 = 0.0f;
		srcArea.x2 -= static_cast<int32_t>( x2 * srcArea.getWidth() );
		clipped = true;
	}
	else if( x2 > 1.0f ) {
		destRect.x2 = static_cast<float>( mWidth );
		srcArea.x2 -= static_cast<int32_t>( ( 1.0f / x2 ) * srcArea.getWidth() );
		clipped = true;
	}

	if( y1 < 0.0f ) {
		destRect.y1 = 0.0f;
		srcArea.y1 -= static_cast<int32_t>( y1 * srcArea.getHeight() );
		clipped = true;
	}
	else if( y1 > 1.0f ) {
		destRect.y1 = static_cast<float>( mHeight );
		srcArea.y1 -= static_cast<int32_t>( ( 1.0f / y1 ) * srcArea.getHeight() );
		clipped = true;
	}

	if( y2 < 0.0f ) {
		destRect.y2 = 0.0f;
		srcArea.y2 -= static_cast<int32_t>( y2 * srcArea.getHeight() );
		clipped = true;
	}
	else if( y2 > 1.0f ) {
		destRect.y2 = static_cast<float>( mHeight );
		srcArea.y2 -= static_cast<int32_t>( ( 1.0f / y2 ) * srcArea.getHeight() );
		clipped = true;
	}

	return clipped;
}

XmlTree	Warp::toXml() const
{
	XmlTree		xml;
	xml.setTag( "warp" );
	switch( mType ) {
	case BILINEAR: xml.setAttribute( "method", "bilinear" ); break;
	case PERSPECTIVE: xml.setAttribute( "method", "perspective" ); break;
	case PERSPECTIVE_BILINEAR: xml.setAttribute( "method", "perspectivebilinear" ); break;
	default: xml.setAttribute( "method", "unknown" ); break;
	}
	xml.setAttribute( "width", mControlsX );
	xml.setAttribute( "height", mControlsY );
	xml.setAttribute( "brightness", mBrightness );

	// add <controlpoint> tags (column-major)
	std::vector<vec2>::const_iterator itr;
	for( itr = mPoints.begin(); itr != mPoints.end(); ++itr ) {
		XmlTree	cp;
		cp.setTag( "controlpoint" );
		cp.setAttribute( "x", ( *itr ).x );
		cp.setAttribute( "y", ( *itr ).y );

		xml.push_back( cp );
	}

	return xml;
}

void Warp::fromXml( const XmlTree &xml )
{
	mControlsX = xml.getAttributeValue<int>( "width", 2 );
	mControlsY = xml.getAttributeValue<int>( "height", 2 );
	mBrightness = xml.getAttributeValue<float>( "brightness", 1.0f );
	// from https://github.com/jhurlbut/Cinder-Warping
	mSrcArea = Area(xml.getAttributeValue<int>("srcAreaX1", -1), xml.getAttributeValue<int>("srcAreaY1", -1), xml.getAttributeValue<int>("srcAreaX2", -1), xml.getAttributeValue<int>("srcAreaY2", -1));

	// load control points
	mPoints.clear();
	for( XmlTree::ConstIter child = xml.begin( "controlpoint" ); child != xml.end(); ++child ) {
		float x = child->getAttributeValue<float>( "x", 0.0f );
		float y = child->getAttributeValue<float>( "y", 0.0f );
		mPoints.push_back( vec2( x, y ) );
	}

	// reconstruct warp
	mIsDirty = true;
}

void Warp::setSize( int w, int h )
{
	mWidth = w;
	mHeight = h;

	mIsDirty = true;
}

void Warp::setSize( const ivec2 &size )
{
	mWidth = size.x;
	mHeight = size.y;

	mIsDirty = true;
}

vec2 Warp::getControlPoint( unsigned index ) const
{
	if( index >= mPoints.size() ) return vec2( 0 );
	return mPoints[index];
}

void Warp::setControlPoint( unsigned index, const vec2 &pos )
{
	if( index >= mPoints.size() ) return;
	mPoints[index] = pos;

	mIsDirty = true;
}

void Warp::moveControlPoint( unsigned index, const vec2 &shift )
{
	if( index >= mPoints.size() ) return;
	mPoints[index] += shift;

	mIsDirty = true;
}

void Warp::selectControlPoint( unsigned index )
{
	if( index >= mPoints.size() || index == mSelected ) return;

	mSelected = index;
	sSelectedTime = app::getElapsedSeconds();
}

void Warp::deselectControlPoint()
{
	mSelected = -1; // since this is an unsigned int, actual value will be 'MAX_INTEGER'
}

unsigned Warp::findControlPoint( const vec2 &pos, float *distance ) const
{
	unsigned index;

	// find closest control point
	float dist = 10.0e6f;

	for( unsigned i = 0; i < mPoints.size(); i++ ) {
		float d = glm::distance( pos, getControlPoint( i ) * mWindowSize );

		if( d < dist ) {
			dist = d;
			index = i;
		}
	}

	*distance = dist;

	return index;
}

void Warp::selectClosestControlPoint( const WarpList &warps, const ivec2 &position )
{
	WarpRef		warp;
	unsigned	i, index;
	float		d, distance = 10.0e6f;

	// find warp and distance to closest control point
	for( WarpConstReverseIter itr = warps.rbegin(); itr != warps.rend(); ++itr ) {
		i = ( *itr )->findControlPoint( position, &d );

		if( d < distance ) {
			distance = d;
			index = i;
			warp = *itr;
		}
	}

	// select the closest control point and deselect all others
	for( WarpConstIter itr = warps.begin(); itr != warps.end(); ++itr ) {
		if( *itr == warp )
			( *itr )->selectControlPoint( index );
		else
			( *itr )->deselectControlPoint();
	}
}

void Warp::setSize( const WarpList &warps, const ivec2 &size )
{
	for( WarpConstIter itr = warps.begin(); itr != warps.end(); ++itr )
		( *itr )->setSize( size );
}

WarpList Warp::readSettings( const DataSourceRef &source )
{
	XmlTree		doc;
	WarpList	warps;

	// try to load the specified xml file
	try { doc = XmlTree( source ); }
	catch( ... ) { return warps; }

	// check if this is a valid file 
	bool isWarp = doc.hasChild( "warpconfig" );
	if( !isWarp ) return warps;

	//
	if( isWarp ) {
		// get first profile
		XmlTree profileXml = doc.getChild( "warpconfig/profile" );

		// iterate maps
		for( XmlTree::ConstIter child = profileXml.begin( "map" ); child != profileXml.end(); ++child ) {
			XmlTree warpXml = child->getChild( "warp" );

			// create warp of the correct type
			std::string method = warpXml.getAttributeValue<std::string>( "method", "unknown" );
			if( method == "bilinear" ) {
				WarpBilinearRef warp( new WarpBilinear() );
				warp->fromXml( warpXml );
				warps.push_back( warp );
			}
			else if( method == "perspective" ) {
				WarpPerspectiveRef warp( new WarpPerspective() );
				warp->fromXml( warpXml );
				warps.push_back( warp );
			}
			else if( method == "perspectivebilinear" ) {
				WarpPerspectiveBilinearRef warp( new WarpPerspectiveBilinear() );
				warp->fromXml( warpXml );
				warps.push_back( warp );
			}
		}
	}

	return warps;
}

void Warp::writeSettings( const WarpList &warps, const DataTargetRef &target )
{
	// create default <profile> (profiles are not yet supported)
	XmlTree			profile;
	profile.setTag( "profile" );
	profile.setAttribute( "name", "default" );

	// 
	for( unsigned i = 0; i < warps.size(); ++i ) {
		// create <map>
		XmlTree			map;
		map.setTag( "map" );
		map.setAttribute( "id", i + 1 );
		map.setAttribute( "display", 1 );	// not supported yet

		// create <warp>
		map.push_back( warps[i]->toXml() );

		// add map to profile
		profile.push_back( map );
	}

	// create config document and root <warpconfig>
	XmlTree			doc;
	doc.setTag( "warpconfig" );
	doc.setAttribute( "version", "1.0" );
	doc.setAttribute( "profile", "default" );

	// add profile to root
	doc.push_back( profile );

	// write file
	doc.write( target );
}

bool Warp::handleMouseMove( WarpList &warps, MouseEvent &event )
{
	// store mouse position 
	sMouse = event.getPos();

	// find and select closest control point
	selectClosestControlPoint( warps, sMouse );

	return false;
}

bool Warp::handleMouseDown( WarpList &warps, MouseEvent &event )
{
	// store mouse position 
	sMouse = event.getPos();

	// find and select closest control point
	selectClosestControlPoint( warps, sMouse );

	for( WarpReverseIter itr = warps.rbegin(); itr != warps.rend() && !event.isHandled(); ++itr )
		( *itr )->mouseDown( event );

	return event.isHandled();
}

bool Warp::handleMouseDrag( WarpList &warps, MouseEvent &event )
{
	for( WarpReverseIter itr = warps.rbegin(); itr != warps.rend() && !event.isHandled(); ++itr )
		( *itr )->mouseDrag( event );

	return event.isHandled();
}

bool Warp::handleMouseUp( WarpList &warps, MouseEvent &event )
{
	return false;
}

bool Warp::handleKeyDown( WarpList &warps, KeyEvent &event )
{
	for( WarpReverseIter itr = warps.rbegin(); itr != warps.rend() && !event.isHandled(); ++itr )
		( *itr )->keyDown( event );

	switch( event.getCode() ) {
	case KeyEvent::KEY_UP:
	case KeyEvent::KEY_DOWN:
	case KeyEvent::KEY_LEFT:
	case KeyEvent::KEY_RIGHT:
		// do not select another control point
		break;
	}

	return event.isHandled();
}

bool Warp::handleKeyUp( WarpList &warps, KeyEvent &event )
{
	return false;
}

bool Warp::handleResize( WarpList &warps )
{
	for( WarpIter itr = warps.begin(); itr != warps.end(); ++itr )
		( *itr )->resize();

	return false;
}

void Warp::mouseMove( cinder::app::MouseEvent &event )
{
	float distance;

	mSelected = findControlPoint( event.getPos(), &distance );
}

void Warp::mouseDown( cinder::app::MouseEvent &event )
{
	if( !sIsEditMode ) return;
	if( mSelected >= mPoints.size() ) return;

	// calculate offset by converting control point from normalized to standard screen space
	ivec2 p = ( getControlPoint( mSelected ) * mWindowSize );
	mOffset = event.getPos() - p;

	event.setHandled( true );
}

void Warp::mouseDrag( cinder::app::MouseEvent &event )
{
	if( !sIsEditMode ) return;
	if( mSelected >= mPoints.size() ) return;

	vec2 m( event.getPos() );
	vec2 p( m.x - mOffset.x, m.y - mOffset.y );

	// set control point in normalized screen space
	setControlPoint( mSelected, p / mWindowSize );

	mIsDirty = true;

	event.setHandled( true );
}

void Warp::mouseUp( cinder::app::MouseEvent &event )
{
}

void Warp::keyDown( KeyEvent &event )
{
	// disable keyboard input when not in edit mode
	if( sIsEditMode ) {
		if( event.getCode() == KeyEvent::KEY_ESCAPE ) {
			// gracefully exit edit mode 
			sIsEditMode = false;
			event.setHandled( true );
			return;
		}
	}
	else return;

	// do not listen to key input if not selected
	if( mSelected >= mPoints.size() ) return;

	switch( event.getCode() ) {
	case KeyEvent::KEY_TAB:
		// select the next or previous (+SHIFT) control point
		if( event.isShiftDown() ) {
			if( mSelected == 0 )
				mSelected = (int) mPoints.size() - 1;
			else
				--mSelected;
			selectControlPoint( mSelected );
		}
		else {
			++mSelected;
			if( mSelected >= mPoints.size() ) mSelected = 0;
			selectControlPoint( mSelected );
		}
		break;
	case KeyEvent::KEY_UP: {
		if( mSelected >= mPoints.size() ) return;
		float step = event.isShiftDown() ? 10.0f : 0.5f;
		mPoints[mSelected].y -= step / mWindowSize.y;
		mIsDirty = true; }
		break;
	case KeyEvent::KEY_DOWN: {
		if( mSelected >= mPoints.size() ) return;
		float step = event.isShiftDown() ? 10.0f : 0.5f;
		mPoints[mSelected].y += step / mWindowSize.y;
		mIsDirty = true; }
		break;
	case KeyEvent::KEY_LEFT: {
		if( mSelected >= mPoints.size() ) return;
		float step = event.isShiftDown() ? 10.0f : 0.5f;
		mPoints[mSelected].x -= step / mWindowSize.x;
		mIsDirty = true; }
		break;
	case KeyEvent::KEY_RIGHT: {
		if( mSelected >= mPoints.size() ) return;
		float step = event.isShiftDown() ? 10.0f : 0.5f;
		mPoints[mSelected].x += step / mWindowSize.x;
		mIsDirty = true; }
		break;
	case KeyEvent::KEY_MINUS:
	case KeyEvent::KEY_KP_MINUS:
		if( mSelected >= mPoints.size() ) return;
		mBrightness = math<float>::max( 0.0f, mBrightness - 0.01f );
		break;
	case KeyEvent::KEY_PLUS:
	case KeyEvent::KEY_KP_PLUS:
		if( mSelected >= mPoints.size() ) return;
		mBrightness = math<float>::min( 1.0f, mBrightness + 0.01f );
		break;
	case KeyEvent::KEY_r:
		if( mSelected >= mPoints.size() ) return;
		reset();
		mIsDirty = true;
		break;
	default:
		return;
	}

	event.setHandled( true );
}

void Warp::keyUp( KeyEvent &event )
{
}

void Warp::resize()
{
	mWindowSize = vec2( getWindowSize() );
	mIsDirty = true;
}

void Warp::drawControlPoint( const vec2 &pt, bool selected, bool attached )
{
	float scale = 0.9f + 0.2f * math<float>::sin( 6.0f * float( app::getElapsedSeconds() - sSelectedTime ) );

	if( selected && attached ) { drawControlPoint( pt, Color( 0.0f, 0.8f, 0.0f ) ); }
	else if( selected ) { drawControlPoint( pt, Color( 0.9f, 0.9f, 0.9f ), scale ); }
	else if( attached ) { drawControlPoint( pt, Color( 0.0f, 0.4f, 0.0f ) ); }
	else { drawControlPoint( pt, Color( 0.4f, 0.4f, 0.4f ) ); }
}

void Warp::drawControlPoint( const vec2 &pt, const Color &clr, float scale )
{
	// enable alpha blending, disable textures
	gl::ScopedBlendAlpha blend(  );
	glDisable( GL_TEXTURE_2D );

	glLineWidth( 1.0f );
	gl::ScopedColor color( ColorA( 0, 0, 0, 0.25f ) );
	gl::drawSolidCircle( pt, 15.0f * scale );

	gl::color( clr );
	gl::drawSolidCircle( pt, 3.0f * scale );

	glLineWidth( 2.0f );
	gl::color( ColorA( clr, 1.0f ) );
	gl::drawStrokedCircle( pt, 8.0f * scale );
	gl::drawStrokedCircle( pt, 12.0f * scale );
}

}
} // namespace ph::warping
