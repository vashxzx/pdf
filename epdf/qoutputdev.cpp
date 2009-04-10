#ifdef __GNUC__
#pragma implementation
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <iostream>

#include "goo/GString.h"
#include "xpdf/Object.h"
#include "xpdf/Stream.h"
#include "xpdf/Link.h"
#include "xpdf/GfxState.h"
#include "xpdf/GfxFont.h"
#include "xpdf/UnicodeMap.h"
#include "xpdf/CharCodeToUnicode.h"
#include "xpdf/FontFile.h"
#include "xpdf/Error.h"
#include "xpdf/TextOutputDev.h"
#include "QOutputDev.h"

#include <QColor>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QPolygon>
#include <QVector>
#include <QMultiHash>
#include <QLabel>
#include <QPen>
#include <QPainterPath>
#include <QMatrix>

#include <qtimer.h>
#include <qapplication.h>
#include <qclipboard.h>

//#define QPDFDBG(x) x		// special debug mode
#define QPDFDBG(x)   		// normal compilation


//------------------------------------------------------------------------
// Constants and macros
//------------------------------------------------------------------------


static inline QColor q_col ( const GfxRGB &rgb )
{
	return QColor ( lrint ( rgb. r * 255 ), lrint ( rgb. g * 255 ), lrint ( rgb. b * 255 ));
}


//------------------------------------------------------------------------
// Font substitutions
//------------------------------------------------------------------------

struct QOutFontSubst {
	char * m_name;
	char * m_sname;
	bool   m_bold;
	bool   m_italic;
	QFont::StyleHint m_hint;
};

static QOutFontSubst qStdFonts [] = {
	{ "Helvetica",             "Helvetica", false, false, QFont::Helvetica },
	{ "Helvetica-Oblique",     "Helvetica", false, true,  QFont::Helvetica },
	{ "Helvetica-Bold",        "Helvetica", true,  false, QFont::Helvetica },
	{ "Helvetica-BoldOblique", "Helvetica", true,  true,  QFont::Helvetica },
	{ "Times-Roman",           "Times",     false, false, QFont::Times },
	{ "Times-Italic",          "Times",     false, true,  QFont::Times },
	{ "Times-Bold",            "Times",     true,  false, QFont::Times },
	{ "Times-BoldItalic",      "Times",     true,  true,  QFont::Times },
	{ "Courier",               "Courier",   false, false, QFont::Courier },
	{ "Courier-Oblique",       "Courier",   false, true,  QFont::Courier },
	{ "Courier-Bold",          "Courier",   true,  false, QFont::Courier },
	{ "Courier-BoldOblique",   "Courier",   true,  true,  QFont::Courier },

	{ "Symbol",                0,           false, false, QFont::AnyStyle },
	{ "Zapf-Dingbats",         0,           false, false, QFont::AnyStyle },

	{ 0,                       0,           false, false, QFont::AnyStyle }
};







QFont QOutputDev::matchFont ( GfxFont *gfxFont, fp_t m11, fp_t m12, fp_t m21, fp_t m22 )
{
	static QMultiHash<QString, QOutFontSubst *> stdfonts;

	// build dict for std. fonts on first invocation
	if ( stdfonts. isEmpty ( )) {
		for ( QOutFontSubst *ptr = qStdFonts; ptr-> m_name; ptr++ ) {
			stdfonts. insert ( QString ( ptr-> m_name ), ptr );
		}
	}

	// compute size and normalized transform matrix
	int size = lrint ( sqrt ( m21 * m21 + m22 * m22 ));

	QPDFDBG( printf ( "SET FONT: Name=%s, Size=%d, Bold=%d, Italic=%d, Mono=%d, Serif=%d, Symbol=%d, CID=%d, EmbFN=%s, M=(%f,%f,%f,%f)\n",
	         (( gfxFont-> getName ( )) ? gfxFont-> getName ( )-> getCString ( ) : "<n/a>" ),
	         size,
	         gfxFont-> isBold ( ),
	         gfxFont-> isItalic ( ),
	         gfxFont-> isFixedWidth ( ),
	         gfxFont-> isSerif ( ),
	         gfxFont-> isSymbolic ( ),
	         gfxFont-> isCIDFont ( ),
	         ( gfxFont-> getEmbeddedFontName ( ) ? gfxFont-> getEmbeddedFontName()-> getCString ( ) : "<n/a>" ),
                          (double) m11, (double) m12, (double) m21, (double) m22 ) );


	QString fname (( gfxFont-> getName ( )) ? gfxFont-> getName ( )-> getCString ( ) : "<n/a>" );

	QFont f;
	f. setPixelSize ( size > 0 ? size : 8 ); // type3 fonts misbehave sometimes

	// fast lookup for std. fonts
	QOutFontSubst *subst = stdfonts.value(fname);

	if ( subst ) {
		if ( subst-> m_sname )
			f. setFamily ( subst-> m_sname );
		f. setStyleHint ( subst-> m_hint, (QFont::StyleStrategy) ( QFont::PreferOutline | QFont::PreferQuality ));
		f. setBold ( subst-> m_bold );
		f. setItalic ( subst-> m_italic );
	}
	else {
		QFont::StyleHint sty;

		if ( gfxFont-> isSerif ( ))
			sty = QFont::Serif;
		else if ( gfxFont-> isFixedWidth ( ))
			sty = QFont::TypeWriter;
		else
			sty = QFont::Helvetica;

		f. setStyleHint ( sty, (QFont::StyleStrategy) ( QFont::PreferOutline | QFont::PreferQuality ));
		f. setBold ( gfxFont-> isBold ( ) > 0 );
		f. setItalic ( gfxFont-> isItalic ( ) > 0 );
		f. setFixedPitch ( gfxFont-> isFixedWidth ( ) > 0 );

		// common specifiers in font names
		if ( fname. contains ( "Oblique" ) || fname. contains ( "Italic" ))
			f. setItalic ( true );
		if ( fname. contains ( "Bold" ))
			f. setWeight ( QFont::Bold );
		if ( fname. contains ( "Demi" ))
			f. setWeight ( QFont::DemiBold );
		if ( fname. contains ( "Light" ))
			f. setWeight ( QFont::Light );
		if ( fname. contains ( "Black" ))
			f. setWeight ( QFont::Black );
	}
	// Treat x-sheared fonts as italic
	if (( m12 > -0.1 ) && ( m12 < 0.1 ) && ((( m21 > -5.0 ) && ( m21 < -0.1 )) || (( m21 > 0.1 ) && ( m21 < 5.0 )))) {
		f. setItalic ( true );
	}
	return f;
}



//------------------------------------------------------------------------
// QOutputDev
//------------------------------------------------------------------------

QOutputDev::QOutputDev ( QWidget *parent ) : QScrollArea (parent)
{
	m_pixmap = NULL;
	m_painter = NULL;
	m_label = new QLabel;
    m_label->setBackgroundRole(QPalette::Base);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_label->setScaledContents(true);
    m_label->resize(0, 0);

    setBackgroundRole(QPalette::Dark);
    setWidget(m_label);
	// create text object
	m_text = new TextPage ( gFalse );
}

QOutputDev::~QOutputDev ( )
{
	delete m_painter;
	delete m_pixmap;
	delete m_text;
}


void QOutputDev::startPage ( int /*pageNum*/, GfxState *state )
{
	delete m_pixmap;
	delete m_painter;

	m_pixmap = new QPixmap ( lrint ( state-> getPageWidth ( )), lrint ( state-> getPageHeight ( )));
	m_painter = new QPainter ( m_pixmap );

	QPDFDBG( printf ( "NEW PIXMAP (%ld x %ld)\n", lrint ( state-> getPageWidth ( )),  lrint ( state-> getPageHeight ( ))));
	
	m_pixmap-> fill ( Qt::white ); // clear window
	m_text-> clear ( ); // cleat text object
	//m_label->setPixmap(*m_pixmap);
	//m_label->adjustSize();
	drawContents();
	m_label->adjustSize();
}

void QOutputDev::endPage ( )
{
        QPDFDBG( printf("End page\n") );
	m_text-> coalesce ( );

        /*
         * I get stupid crashes after endPage is called and then we do clipping
         * and other stuff.....
         */
#if 0
	delete m_painter;
	m_painter = 0;
#endif

	//updateContents ( 0, 0, contentsWidth ( ), contentsHeight ( ));
	//m_label->setPixmap(*m_pixmap);
	drawContents();
}

void QOutputDev::drawLink ( Link *link, Catalog */*catalog*/ )
{
	fp_t x1, y1, x2, y2, w;

	link-> getBorder ( &x1, &y1, &x2, &y2, &w );

	if ( w > 0 ) {
		int x, y, dx, dy;

		cvtUserToDev ( x1, y1, &x,  &y );
		cvtUserToDev ( x2, y2, &dx, &dy );

		QPen oldpen = m_painter->pen( );
		m_painter-> setPen ( QColor(Qt::blue) );
		m_painter-> drawRect ( x, y, dx, dy );
		m_painter-> setPen ( oldpen );
	}
}

void QOutputDev::saveState ( GfxState */*state*/ )
{
        if ( ! m_painter )
            return;

	QPDFDBG( printf ( "SAVE (CLIP=%d/%d)\n",  m_painter-> hasClipping ( ), !m_painter-> clipRegion ( ). isEmpty ( )));

        m_painter-> save ( );
}

void QOutputDev::restoreState ( GfxState */*state*/ )
{
	if( ! m_painter )
	    return;

	m_painter-> restore ( );

//	m_painter-> setClipRegion ( QRect ( 0, 0, m_pixmap-> width ( ), m_pixmap-> height ( )));
//	m_painter-> setClipping ( false );
	QPDFDBG ( printf ( "RESTORE (CLIP=%d/%d)\n", m_painter-> hasClipping ( ), !m_painter-> clipRegion ( ). isEmpty ( )));
}

void QOutputDev::updateAll ( GfxState *state )
{
	updateLineAttrs ( state, gTrue );
//	updateFlatness ( state );
//	updateMiterLimit ( state );
	updateFillColor ( state );
	updateStrokeColor ( state );
	updateFont ( state );
}

void QOutputDev::updateCTM ( GfxState *state, fp_t /*m11*/, fp_t /*m12*/, fp_t /*m21*/, fp_t /*m22*/, fp_t /*m31*/, fp_t /*m32*/ )
{
	updateLineAttrs ( state, gTrue );
}

void QOutputDev::updateLineDash ( GfxState *state )
{
	updateLineAttrs ( state, gTrue );
}

void QOutputDev::updateFlatness ( GfxState */*state*/ )
{
	// not supported
	QPDFDBG( printf ( "updateFlatness not supported !\n" ));
}

void QOutputDev::updateLineJoin ( GfxState *state )
{
	updateLineAttrs ( state, gFalse );
}

void QOutputDev::updateLineCap ( GfxState *state )
{
	updateLineAttrs ( state, gFalse );
}

// unimplemented
void QOutputDev::updateMiterLimit ( GfxState */*state*/ )
{
	QPDFDBG( printf ( "updateMiterLimit not supported !\n" ));
}

void QOutputDev::updateLineWidth ( GfxState *state )
{
	updateLineAttrs ( state, gFalse );
}

void QOutputDev::updateLineAttrs ( GfxState *state, GBool updateDash )
{
	fp_t *dashPattern;
	int dashLength;
	fp_t dashStart;

	Qt::PenCapStyle  cap;
	Qt::PenJoinStyle join;
	int width;

	width = lrint ( state-> getTransformedLineWidth ( ));

	switch ( state-> getLineCap ( )) {
		case 0: cap = Qt::FlatCap; break;
		case 1: cap = Qt::RoundCap; break;
		case 2: cap = Qt::SquareCap; break;
		default:
			qWarning ( "Bad line cap style (%d)\n", state-> getLineCap ( ));
			cap = Qt::FlatCap;
			break;
	}

	switch (state->getLineJoin()) {
		case 0: join = Qt::MiterJoin; break;
		case 1: join = Qt::RoundJoin; break;
		case 2: join = Qt::BevelJoin; break;
		default:
			qWarning ( "Bad line join style (%d)\n", state->getLineJoin ( ));
			join = Qt::MiterJoin;
			break;
	}

	state-> getLineDash ( &dashPattern, &dashLength, &dashStart );

	QColor oldcol = m_painter-> pen ( ). color ( );
	GfxRGB rgb;

	state-> getStrokeRGB ( &rgb );
	oldcol = q_col ( rgb );

	m_painter-> setPen ( QPen ( QBrush(oldcol), width, 
				dashLength > 0 ? Qt::DashLine : Qt::SolidLine, cap, join ));

	if ( updateDash && ( dashLength > 0 )) {
		// Not supported by QT
/*
		char dashList[20];
		if (dashLength > 20)
			dashLength = 20;
		for ( int i = 0; i < dashLength; ++i ) {
			dashList[i] = xoutRound(state->transformWidth(dashPattern[i]));
			if (dashList[i] == 0)
				dashList[i] = 1;
		}
		XSetDashes(display, strokeGC, xoutRound(dashStart), dashList, dashLength);
*/
	}
}

void QOutputDev::updateFillColor ( GfxState *state )
{
	GfxRGB rgb;
	state-> getFillRGB ( &rgb );

	m_painter-> setBrush ( QBrush(q_col ( rgb )));
}

void QOutputDev::updateStrokeColor ( GfxState *state )
{
	GfxRGB rgb;
	state-> getStrokeRGB ( &rgb );

	QPen pen = m_painter-> pen ( );
	pen. setColor ( q_col ( rgb ));
	m_painter-> setPen ( pen );
}

void QOutputDev::updateFont ( GfxState *state )
{
	fp_t m11, m12, m21, m22;
	GfxFont *gfxFont = state-> getFont ( );

	if ( !gfxFont )
		return;

	state-> getFontTransMat ( &m11, &m12, &m21, &m22 );
	m11 *= state-> getHorizScaling ( );
	m12 *= state-> getHorizScaling ( );

	QFont font = matchFont ( gfxFont, m11, m12, m21, m22 );

	m_painter-> setFont ( font );
	m_text-> updateFont ( state );
}

void QOutputDev::stroke ( GfxState *state )
{
	QPolygon points;
	QVector<int> lengths;
	QPoint *tmp = NULL;

	// transform points
	int n = convertPath ( state, points, lengths );
	tmp = points.data();
	QPDFDBG( printf ( "DRAWING: %d POLYS\n", n ));

	// draw each subpath
	int j = 0;
	for ( int i = 0; i < n; i++ ) {
		int len = lengths [i];

		if ( len >= 2 ) {
			QPDFDBG( printf ( " - POLY %d: ", i ));
			QPDFDBG( for ( int ii = 0; ii < len; ii++ ))
				QPDFDBG( printf ( "(%d/%d) ", points.point(j+ii). x ( ), 
						points.point(j+ii). y ( )));
			QPDFDBG( printf ( "\n" ));
			
			m_painter-> drawPolyline (tmp+j, len);
		}
		j += len;
	}
	qApp-> processEvents ( );
}

void QOutputDev::fill ( GfxState *state )
{
	doFill ( state, true );
}

void QOutputDev::eoFill ( GfxState *state )
{
	doFill ( state, false );
}

//
//  X doesn't color the pixels on the right-most and bottom-most
//  borders of a polygon.  This means that one-pixel-thick polygons
//  are not colored at all.  I think this is supposed to be a
//  feature, but I can't figure out why.  So after it fills a
//  polygon, it also draws lines around the border.  This is done
//  only for single-component polygons, since it's not very
//  compatible with the compound polygon kludge (see convertPath()).
//
void QOutputDev::doFill ( GfxState *state, bool winding )
{
	QPolygon points;
	QVector<int> lengths;

	QPoint *tmp = NULL;
	// transform points
	int n = convertPath ( state, points, lengths );
	tmp = points.data();
	QPDFDBG( printf ( "FILLING: %d POLYS\n", n ));

	QPen oldpen = m_painter-> pen ( );
	m_painter-> setPen ( QPen ( Qt::NoPen ));

	// draw each subpath
	int j = 0;
	for ( int i = 0; i < n; i++ ) {
		int len = lengths [i];

		if ( len >= 3 ) {
			QPDFDBG( printf ( " - POLY %d: ", i ));
			QPDFDBG( for ( int ii = 0; ii < len; ii++ ))
				QPDFDBG( printf ( "(%d/%d) ", points.point(j+ii). x ( ), 
						points.point(j+ii). y ( )));
			QPDFDBG( printf ( "\n" ));

			m_painter-> drawPolygon ( tmp+j, len, 
								winding ? Qt::WindingFill:Qt::OddEvenFill);
		}
		j += len;
	}
	m_painter-> setPen ( oldpen );

	qApp-> processEvents ( );
}

void QOutputDev::clip ( GfxState *state )
{
	doClip ( state, true );
}

void QOutputDev::eoClip ( GfxState *state )
{
	doClip ( state, false );
}

void QOutputDev::doClip ( GfxState *state, bool winding )
{
	QPolygon points;
	QVector<int> lengths;

	// transform points
	int n = convertPath ( state, points, lengths );
	
	QRegion region;

	QPDFDBG( printf ( "CLIPPING: %d POLYS\n", n ));

	// draw each subpath
	int j = 0;
	for ( int i = 0; i < n; i++ ) {
		int len = lengths [i];

		if ( len >= 3 ) {

			QPDFDBG( printf ( " - POLY %d: ", i ));
			QPDFDBG( for ( int ii = 0; ii < len; ii++ ) printf ( "(%d/%d) ", points.point(j+ii). x ( ), points.point(j+ii). y ( )));
			QPDFDBG( printf ( "\n" ));

			region |= QRegion ( QPolygon(points.mid(j,len)), 
								winding ? Qt::WindingFill : Qt::OddEvenFill );

		}
		j += len;
	}

	if ( m_painter && m_painter-> hasClipping ( ))
		region &= m_painter-> clipRegion ( );

//	m_painter-> setClipRegion ( region );
//	m_painter-> setClipping ( true );

//	m_painter-> fillRect ( 0, 0, m_pixmap-> width ( ), m_pixmap-> height ( ), red );
//	m_painter-> drawText ( points [0]. x ( ) + 10, points [0]. y ( ) + 10, "Bla bla" );
	qApp-> processEvents ( );
}

//
// Transform points in the path and convert curves to line segments.
// Builds a set of subpaths and returns the number of subpaths.
// If <fillHack> is set, close any unclosed subpaths and activate a
// kludge for polygon fills:  First, it divides up the subpaths into
// non-overlapping polygons by simply comparing bounding rectangles.
// Then it connects subaths within a single compound polygon to a single
// point so that X can fill the polygon (sort of).
//
int QOutputDev::convertPath ( GfxState *state, QPolygon &points, QVector<int> &lengths )
{
	GfxPath *path = state-> getPath ( );
	int n = path-> getNumSubpaths ( );

	lengths. resize ( n );

	// do each subpath
	for ( int i = 0; i < n; i++ ) {
		// transform the points
		lengths [i] = convertSubpath ( state, path-> getSubpath ( i ), points );
	}

	return n;
}

//
// Transform points in a single subpath and convert curves to line
// segments.
//
int QOutputDev::convertSubpath ( GfxState *state, GfxSubpath *subpath, QPolygon &points )
{
	int oldcnt = points. count ( );

	fp_t x0, y0, x1, y1, x2, y2, x3, y3;

	int m = subpath-> getNumPoints ( );
	int i = 0;

	while ( i < m ) {
		if ( i >= 1 && subpath-> getCurve ( i )) {
			state-> transform ( subpath-> getX ( i - 1 ), subpath-> getY ( i - 1 ), &x0, &y0 );
			state-> transform ( subpath-> getX ( i ),     subpath-> getY ( i ),     &x1, &y1 );
			state-> transform ( subpath-> getX ( i + 1 ), subpath-> getY ( i + 1 ), &x2, &y2 );
			state-> transform ( subpath-> getX ( i + 2 ), subpath-> getY ( i + 2 ), &x3, &y3 );

			QPainterPath path;
			path.moveTo( lrint(x0), lrint(y0) );
			path.cubicTo( lrint(x1),lrint(y1), lrint(x2),lrint(y2),
							lrint(x3),lrint(y3) );
			QPolygon tmp = path.toFillPolygon().toPolygon();
			tmp.remove(tmp.size() - 1);
			 
			points. putPoints ( points.count ( ), tmp.count ( ), tmp );

			i += 3;
		}
		else {
			state-> transform ( subpath-> getX ( i ), subpath-> getY ( i ), &x1, &y1 );

			points. putPoints ( points.count ( ), 1, lrint ( x1 ), lrint ( y1 ));
			++i;
		}
	}
	return points. count ( ) - oldcnt;
}


void QOutputDev::beginString ( GfxState *state, GString */*s*/ )
{
	m_text-> beginString ( state );
}

void QOutputDev::endString ( GfxState */*state*/ )
{
	m_text-> endString ( );
}

void QOutputDev::drawChar ( GfxState *state, fp_t x, fp_t y,
                           fp_t dx, fp_t dy, fp_t originX, fp_t originY,
                           CharCode code, Unicode *u, int uLen )
{
	fp_t x1, y1, dx1, dy1;

	if ( uLen > 0 )
		m_text-> addChar ( state, x, y, dx, dy, u, uLen );

	// check for invisible text -- this is used by Acrobat Capture
	if (( state-> getRender ( ) & 3 ) == 3 ) {
		return;
	}

	x -= originX;
	y -= originY;
	state-> transform      ( x,  y,  &x1,  &y1 );
	state-> transformDelta ( dx, dy, &dx1, &dy1 );


	if ( uLen > 0 ) {
		QString str;
		QFontMetrics fm = m_painter-> fontMetrics ( );

		for ( int i = 0; i < uLen; i++ ) {
			QChar c = QChar ( u [i] );

			if ( fm. inFont ( c )) {
				str [i] = QChar ( u [i] );
			}
			else {
				str [i] = ' ';
				QPDFDBG( printf ( "CHARACTER NOT IN FONT: %hx\n", c. unicode ( )));
			}
		}

		if (( uLen == 1 ) && ( str [0] == ' ' ))
			return;


		fp_t m11, m12, m21, m22;

		state-> getFontTransMat ( &m11, &m12, &m21, &m22 );
		m11 *= state-> getHorizScaling ( );
		m12 *= state-> getHorizScaling ( );

		fp_t fsize = m_painter-> font ( ). pixelSize ( );

#ifndef QT_NO_TRANSFORMATIONS
		QMatrix oldmat;

		bool dorot = (( m12 < -0.1 ) || ( m12 > 0.1 )) && (( m21 < -0.1 ) || ( m21 > 0.1 ));

		if ( dorot ) {
			oldmat = m_painter-> worldMatrix ( );

			std::cerr << std::endl << "ROTATED: " << m11 << ", " << m12 << ", " << m21 << ", " << m22 << " / SIZE: " << fsize << " / TEXT: " << str.toLocal8Bit ( ) << endl << endl;

			QMatrix mat ( lrint ( m11 / fsize ), lrint ( m12 / fsize ), -lrint ( m21 / fsize ), -lrint ( m22 / fsize ), lrint ( x1 ), lrint ( y1 ));

			m_painter-> setWorldMatrix ( mat );

			x1 = 0;
			y1 = 0;
		}
#endif

		QPen oldpen = m_painter-> pen ( );

		if (!( state-> getRender ( ) & 1 )) {
			QPen fillpen = oldpen;

			fillpen. setColor ( m_painter-> brush ( ). color ( ));
			m_painter-> setPen ( fillpen );
		}

		if ( fsize > 5 )
			m_painter-> drawText ( lrint ( x1 ), lrint ( y1 ), str );
		else
			m_painter-> fillRect ( lrint ( x1 ), lrint ( y1 ), lrint ( QMAX( fp_t(1), dx1 )), lrint ( QMAX( fsize, dy1 )), m_painter-> pen ( ). color ( ));

		m_painter-> setPen ( oldpen );

#ifndef QT_NO_TRANSFORMATIONS
		if ( dorot )
			m_painter-> setWorldMatrix ( oldmat );
#endif

		QPDFDBG( printf ( "DRAW TEXT: \"%s\" at (%ld/%ld)\n", str.toLocal8Bit ( ). data ( ), lrint ( x1 ), lrint ( y1 )));
	}
	else if ( code != 0 ) {
		// some PDF files use CID 0, which is .notdef, so just ignore it
		qWarning ( "Unknown character (CID=%d Unicode=%hx)\n", code, (unsigned short) ( uLen > 0 ? u [0] : (Unicode) 0 ));
	}
	qApp-> processEvents ( );
}



void QOutputDev::drawImageMask ( GfxState *state, Object */*ref*/, Stream *str, int width, int height, GBool invert, GBool inlineImg )
{
	// get CTM, check for singular matrix
	fp_t *ctm = state-> getCTM ( );

	if ( fabs ( ctm [0] * ctm [3] - ctm [1] * ctm [2] ) < 0.000001 ) {
		qWarning ( "Singular CTM in drawImage\n" );

		if ( inlineImg ) {
			str-> reset ( );
			int j = height * (( width + 7 ) / 8 );
			for ( int i = 0; i < j; i++ )
				str->getChar();

			str->close();
		}
		return;
	}

	GfxRGB rgb;
	state-> getFillRGB ( &rgb );
	uint val = ( lrint ( rgb. r * 255 ) & 0xff ) << 16 | ( lrint ( rgb. g * 255 ) & 0xff ) << 8 | ( lrint ( rgb. b * 255 ) & 0xff );


	QImage img ( width, height, QImage::Format_ARGB32 );

	QPDFDBG( printf ( "IMAGE MASK (%dx%d)\n", width, height ));

	// initialize the image stream
	ImageStream *imgStr = new ImageStream ( str, width, 1, 1 );
	imgStr-> reset ( );

	int scanlines = 0;

	if ( ctm [3] > 0 )
		scanlines += ( height - 1 );

	for ( int y = 0; y < height; y++ ) {
		QRgb *scanline = (QRgb *) img.scanLine(scanlines);

		if ( ctm [0] < 0 )
			scanline += ( width - 1 );

		for ( int x = 0; x < width; x++ ) {
			Guchar alpha;

			imgStr-> getPixel ( &alpha );

			if ( invert )
				alpha ^= 1;

			*scanline = ( alpha == 0 ) ? 0xff000000 | val : val;

			ctm [0] < 0 ? scanline-- : scanline++;
		}
		ctm [3] > 0 ? scanlines-- : scanlines++;

		qApp-> processEvents ( );
	}

#ifndef QT_NO_TRANSFORMATIONS
	QMatrix mat ( ctm [0] / width, ctm [1], ctm [2], ctm [3] / height, ctm [4], ctm [5] );

	std::cerr << "MATRIX T=" << mat. dx ( ) << "/" << mat. dy ( ) << std::endl
	         << " - M=" << mat. m11 ( ) << "/" << mat. m12 ( ) << "/" << mat. m21 ( ) << "/" << mat. m22 ( ) << std::endl;

	QMatrix oldmat = m_painter-> worldMatrix ( );
	m_painter-> setWorldMatrix ( mat, true );

#ifdef QWS
	QPixmap pm;
	pm. fromImage ( img );
	m_painter-> drawPixmap ( 0, 0, pm );
#else
	m_painter-> drawImage ( QPoint ( 0, 0 ), img );
#endif

	m_painter-> setWorldMatrix ( oldmat );

#else
	if (( ctm [1] < -0.1 ) || ( ctm [1] > 0.1 ) || ( ctm [2] < -0.1 ) || ( ctm [2] > 0.1 )) {
		QPDFDBG( printf (  "### ROTATED / SHEARED / ETC -- CANNOT DISPLAY THIS IMAGE\n" ));
	}
	else {
		int x = lrint ( ctm [4] );
		int y = lrint ( ctm [5] );

		int w = lrint ( ctm [0] );
		int h = lrint ( ctm [3] );

		if ( w < 0 ) {
			x += w;
			w = -w;
		}
		if ( h < 0 ) {
			y += h;
			h = -h;
		}

		QPDFDBG( printf ( "DRAWING IMAGE MASKED: %d/%d - %dx%d\n", x, y, w, h ));

		img = img.scaled(w, h , 
			Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		qApp-> processEvents ( );
		m_painter-> drawImage ( x, y, img );
	}

#endif

	delete imgStr;
	qApp-> processEvents ( );
}


void QOutputDev::drawImage(GfxState *state, Object */*ref*/, Stream *str, int width, int height, GfxImageColorMap *colorMap, int *maskColors, GBool inlineImg )
{
	int nComps, nVals, nBits;

	// image parameters
	nComps = colorMap->getNumPixelComps ( );
	nVals = width * nComps;
	nBits = colorMap-> getBits ( );

	// get CTM, check for singular matrix
	fp_t *ctm = state-> getCTM ( );

	if ( fabs ( ctm [0] * ctm [3] - ctm [1] * ctm [2] ) < 0.000001 ) {
		qWarning ( "Singular CTM in drawImage\n" );

		if ( inlineImg ) {
			str-> reset ( );
			int j = height * (( nVals * nBits + 7 ) / 8 );
			for ( int i = 0; i < j; i++ )
				str->getChar();

			str->close();
		}
		return;
	}

	QImage img ( width, height, QImage::Format_RGB32);

	if ( maskColors )
		img = img.convertToFormat(QImage::Format_ARGB32);

	QPDFDBG( printf ( "IMAGE (%dx%d)\n", width, height ));

	// initialize the image stream
	ImageStream *imgStr = new ImageStream ( str, width, nComps, nBits );
	imgStr-> reset ( );

	Guchar pixBuf [gfxColorMaxComps];
	GfxRGB rgb;


	int scanlines = 0;

	if ( ctm [3] > 0 )
		scanlines += ( height - 1 );

	for ( int y = 0; y < height; y++ ) {
		QRgb *scanline = (QRgb *)img.scanLine(scanlines);

		if ( ctm [0] < 0 )
			scanline += ( width - 1 );

		for ( int x = 0; x < width; x++ ) {
			imgStr-> getPixel ( pixBuf );
			colorMap-> getRGB ( pixBuf, &rgb );

			uint val = ( lrint ( rgb. r * 255 ) & 0xff ) << 16 | ( lrint ( rgb. g * 255 ) & 0xff ) << 8 | ( lrint ( rgb. b * 255 ) & 0xff );

			if ( maskColors ) {
				for ( int k = 0; k < nComps; ++k ) {
					if (( pixBuf [k] < maskColors [2 * k] ) || ( pixBuf [k] > maskColors [2 * k] )) {
						val |= 0xff000000;
						break;
					}
				}
			}
			*scanline = val;

			ctm [0] < 0 ? scanline-- : scanline++;
		}
		ctm [3] > 0 ? scanlines-- : scanlines++;

		qApp-> processEvents ( );
	}


#ifndef QT_NO_TRANSFORMATIONS
	QMatrix mat ( ctm [0] / width, ctm [1], ctm [2], ctm [3] / height, ctm [4], ctm [5] );

	std::cerr << "MATRIX T=" << mat. dx ( ) << "/" << mat. dy ( ) << std::endl
	         << " - M=" << mat. m11 ( ) << "/" << mat. m12 ( ) << "/" << mat. m21 ( ) << "/" << mat. m22 ( ) << std::endl;

	QMatrix oldmat = m_painter-> worldMatrix ( );
	m_painter-> setWorldMatrix ( mat, true );

#ifdef QWS
	QPixmap pm;
	pm.fromImage ( img );
	m_painter-> drawPixmap ( 0, 0, pm );
#else
	m_painter-> drawImage ( QPoint ( 0, 0 ), img );
#endif

	m_painter-> setWorldMatrix ( oldmat );

#else // QT_NO_TRANSFORMATIONS

	if (( ctm [1] < -0.1 ) || ( ctm [1] > 0.1 ) || ( ctm [2] < -0.1 ) || ( ctm [2] > 0.1 )) {
		QPDFDBG( printf ( "### ROTATED / SHEARED / ETC -- CANNOT DISPLAY THIS IMAGE\n" ));
	}
	else {
		int x = lrint ( ctm [4] );
		int y = lrint ( ctm [5] );

		int w = lrint ( ctm [0] );
		int h = lrint ( ctm [3] );

		if ( w < 0 ) {
			x += w;
			w = -w;
		}
		if ( h < 0 ) {
			y += h;
			h = -h;
		}

		QPDFDBG( printf ( "DRAWING IMAGE: %d/%d - %dx%d\n", x, y, w, h ));

		img = img.scaled(w, h , 
			Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		qApp-> processEvents ( );
		m_painter-> drawImage ( x, y, img );
	}

#endif

	delete imgStr;
	qApp-> processEvents ( );
}



bool QOutputDev::findText ( const QString &str, QRect &r, bool top, bool bottom )
{
	int l, t, w, h;
	r. getRect ( &l, &t, &w, &h );

	bool res = findText ( str, l, t, w, h, top, bottom );

	r. setRect ( l, t, w, h );
	return res;
}

bool QOutputDev::findText ( const QString &str, int &l, int &t, int &w, int &h, bool top, bool bottom )
{
	bool found = false;
	uint len = str. length ( );
	Unicode *s = new Unicode [len];

	for ( uint i = 0; i < len; i++ )
		s [i] = str [i]. unicode ( );

	fp_t x1 = (fp_t) l;
	fp_t y1 = (fp_t) t;
	fp_t x2 = (fp_t) l + w - 1;
	fp_t y2 = (fp_t) t + h - 1;

	if ( m_text-> findText ( s, len, top, bottom, &x1, &y1, &x2, &y2 )) {
		l = lrint ( x1 );
		t = lrint ( y1 );
		w = lrint ( x2 ) - l + 1;
		h = lrint ( y2 ) - t + 1;
		found = true;
	}
	delete [] s;

	return found;
}

GBool QOutputDev::findText ( Unicode *s, int len, GBool top, GBool bottom, int *xMin, int *yMin, int *xMax, int *yMax )
{
	bool found = false;
	fp_t xMin1 = (double) *xMin;
	fp_t yMin1 = (double) *yMin;
	fp_t xMax1 = (double) *xMax;
	fp_t yMax1 = (double) *yMax;

	if ( m_text-> findText ( s, len, top, bottom, &xMin1, &yMin1, &xMax1, &yMax1 )) {
		*xMin = lrint ( xMin1 );
		*xMax = lrint ( xMax1 );
		*yMin = lrint ( yMin1 );
		*yMax = lrint ( yMax1 );
		found = true;
	}
	return found;
}

QString QOutputDev::getText ( int l, int t, int w, int h )
{
	GString *gstr = m_text-> getText ( l, t, l + w - 1, t + h - 1 );
	QString str = gstr-> getCString ( );
	delete gstr;
	return str;
}

QString QOutputDev::getText ( const QRect &r )
{
	return getText ( r. left ( ), r. top ( ), r. width ( ), r. height ( ));
}



void QOutputDev::drawContents ( )
{
	if ( m_pixmap )
		m_label->setPixmap(*m_pixmap);
}
