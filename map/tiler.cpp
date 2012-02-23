#include "../base/SRC_FIRST.hpp"
#include "tiler.hpp"
#include "../indexer/mercator.hpp"
#include "../indexer/scales.hpp"
#include "../base/logging.hpp"
#include "../platform/platform.hpp"

Tiler::RectInfo::RectInfo()
  : m_drawScale(0), m_tileScale(0), m_x(0), m_y(0)
{}

Tiler::RectInfo::RectInfo(int drawScale, int tileScale, int x, int y)
  : m_drawScale(drawScale), m_tileScale(tileScale), m_x(x), m_y(y)
{
  initRect();
}

void Tiler::RectInfo::initRect()
{
  int k = 1 << m_tileScale;

  double rectSizeX = (MercatorBounds::maxX - MercatorBounds::minX) / k;
  double rectSizeY = (MercatorBounds::maxY - MercatorBounds::minY) / k;

  m_rect.setMinX(m_x * rectSizeX);
  m_rect.setMaxX((m_x + 1) * rectSizeX);
  m_rect.setMinY(m_y * rectSizeY);
  m_rect.setMaxY((m_y + 1) * rectSizeY);
}

LessByScaleAndDistance::LessByScaleAndDistance(m2::PointD const & pt)
  : m_pt(pt)
{
}

bool LessByScaleAndDistance::operator()(Tiler::RectInfo const & l, Tiler::RectInfo const & r)
{
  if (l.m_drawScale != r.m_drawScale)
    return l.m_drawScale < r.m_drawScale;
  if (l.m_tileScale != r.m_tileScale)
    return l.m_tileScale < r.m_tileScale;

  return l.m_rect.Center().Length(m_pt) < r.m_rect.Center().Length(m_pt);
}

bool operator<(Tiler::RectInfo const & l, Tiler::RectInfo const & r)
{
  if (l.m_drawScale != r.m_drawScale)
    return l.m_drawScale < r.m_drawScale;
  if (l.m_tileScale != r.m_tileScale)
    return l.m_tileScale < r.m_tileScale;
  if (l.m_y != r.m_y)
    return l.m_y < r.m_y;
  if (l.m_x != r.m_x)
    return l.m_x < r.m_x;
  return false;
}

bool operator==(Tiler::RectInfo const & l, Tiler::RectInfo const & r)
{
  return (l.m_y == r.m_y)
      && (l.m_x == r.m_x)
      && (l.m_drawScale == r.m_drawScale)
      && (l.m_tileScale == r.m_tileScale);
}

int Tiler::getDrawScale(ScreenBase const & s, int ts, double k) const
{
  ScreenBase tmpS = s;
  tmpS.Rotate(-tmpS.GetAngle());

  size_t tileSize = min(static_cast<size_t>(ts / 1.05), (size_t)(512 * k));

  m2::RectD glbRect;
  m2::PointD pxCenter = tmpS.PixelRect().Center();
  tmpS.PtoG(m2::RectD(pxCenter - m2::PointD(tileSize / 2, tileSize / 2),
                      pxCenter + m2::PointD(tileSize / 2, tileSize / 2)),
            glbRect);

  double glbRectSize = min(glbRect.SizeX(), glbRect.SizeY());

  int res = static_cast<int>(ceil(log((MercatorBounds::maxX - MercatorBounds::minX) / glbRectSize) / log(2.0)));

  return res > scales::GetUpperScale() ? scales::GetUpperScale() : res;
}

int Tiler::getTileScale(ScreenBase const & s, int ts) const
{
  ScreenBase tmpS = s;
  tmpS.Rotate(-tmpS.GetAngle());

  /// slightly smaller than original to produce "antialiasing" effect using bilinear filtration.
  size_t tileSize = static_cast<size_t>(ts / 1.05);

  m2::RectD glbRect;
  m2::PointD pxCenter = tmpS.PixelRect().Center();
  tmpS.PtoG(m2::RectD(pxCenter - m2::PointD(tileSize / 2, tileSize / 2),
                      pxCenter + m2::PointD(tileSize / 2, tileSize / 2)),
            glbRect);

  double glbRectSize = min(glbRect.SizeX(), glbRect.SizeY());

  int res = static_cast<int>(ceil(log((MercatorBounds::maxX - MercatorBounds::minX) / glbRectSize) / log(2.0)));

  return res;
}

void Tiler::seed(ScreenBase const & screen, m2::PointD const & centerPt)
{
  m_screen = screen;
  m_centerPt = centerPt;

  m_drawScale = getDrawScale(screen, GetPlatform().TileSize(), 1);
  m_tileScale = getTileScale(screen, GetPlatform().TileSize());
}

void Tiler::tiles(vector<RectInfo> & tiles, int depth)
{
  if (m_tileScale == 0)
    return;

  /// clearing previous coverage
  tiles.clear();

  if (m_tileScale - depth < 0)
    depth = m_tileScale;

  for (unsigned i = 0; i < depth; ++i)
  {
    int pow = depth - 1 - i;

    int scale = 1 << pow;

    int tileSize = GetPlatform().TileSize() * scale;

    int tileScale = getTileScale(m_screen, tileSize);
    int drawScale = getDrawScale(m_screen, tileSize, scale);

    double rectSizeX = (MercatorBounds::maxX - MercatorBounds::minX) / (1 << tileScale);
    double rectSizeY = (MercatorBounds::maxY - MercatorBounds::minY) / (1 << tileScale);

    /// calculating coverage on the global rect, which corresponds to the
    /// pixel rect, which in ceiled to tileSize

    m2::AnyRectD const & globalRect = m_screen.GlobalRect();
    m2::RectD const clipRect = m_screen.GlobalRect().GetGlobalRect();

    int minTileX = static_cast<int>(floor(clipRect.minX() / rectSizeX));
    int maxTileX = static_cast<int>(ceil(clipRect.maxX() / rectSizeX));
    int minTileY = static_cast<int>(floor(clipRect.minY() / rectSizeY));
    int maxTileY = static_cast<int>(ceil(clipRect.maxY() / rectSizeY));

    /// generating new coverage

    for (int tileY = minTileY; tileY < maxTileY; ++tileY)
      for (int tileX = minTileX; tileX < maxTileX; ++tileX)
      {
        m2::RectD tileRect(tileX * rectSizeX,
                           tileY * rectSizeY,
                           (tileX + 1) * rectSizeX,
                           (tileY + 1) * rectSizeY);

        if (globalRect.IsIntersect(m2::AnyRectD(tileRect)))
          tiles.push_back(RectInfo(drawScale, tileScale, tileX, tileY));
      }
  }

  /// sorting coverage elements
  sort(tiles.begin(), tiles.end(), LessByScaleAndDistance(m_centerPt));
}

Tiler::Tiler()
  : m_drawScale(0),
    m_tileScale(0)
{}

int Tiler::drawScale() const
{
  return m_drawScale;
}

int Tiler::tileScale() const
{
  return m_tileScale;
}

bool Tiler::isLeaf(RectInfo const & ri) const
{
  return (ri.m_drawScale == m_drawScale) && (ri.m_tileScale == m_tileScale);
}
