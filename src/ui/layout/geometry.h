#ifndef XER_UI_LAYOUT_GEOMETRY_H
#define XER_UI_LAYOUT_GEOMETRY_H

namespace XER::UI::Layout {

/**
 * Point - 2D point representation
 */
struct Point {
    float x;
    float y;

    Point() : x(0), y(0) {}
    Point(float x, float y) : x(x), y(y) {}

    Point operator+(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }

    Point operator-(const Point& other) const {
        return Point(x - other.x, y - other.y);
    }
};

/**
 * Size - 2D size representation
 */
struct Size {
    float width;
    float height;

    Size() : width(0), height(0) {}
    Size(float width, float height) : width(width), height(height) {}

    bool operator==(const Size& other) const {
        return width == other.width && height == other.height;
    }

    bool operator!=(const Size& other) const {
        return !(*this == other);
    }

    Size operator+(const Size& other) const {
        return Size(width + other.width, height + other.height);
    }

    Size operator-(const Size& other) const {
        return Size(width - other.width, height - other.height);
    }
};

/**
 * Rect - 2D rectangle representation
 */
struct Rect {
    float x;
    float y;
    float width;
    float height;

    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(float x, float y, float width, float height)
        : x(x), y(y), width(width), height(height) {}

    float Left() const { return x; }
    float Top() const { return y; }
    float Right() const { return x + width; }
    float Bottom() const { return y + height; }

    Point TopLeft() const { return Point(x, y); }
    Point TopRight() const { return Point(x + width, y); }
    Point BottomLeft() const { return Point(x, y + height); }
    Point BottomRight() const { return Point(x + width, y + height); }

    Size GetSize() const { return Size(width, height); }

    bool Contains(const Point& point) const {
        return point.x >= x && point.x < x + width &&
               point.y >= y && point.y < y + height;
    }

    bool Contains(const Rect& other) const {
        return other.x >= x && other.y >= y &&
               other.x + other.width <= x + width &&
               other.y + other.height <= y + height;
    }

    bool Intersects(const Rect& other) const {
        return x < other.x + other.width &&
               x + width > other.x &&
               y < other.y + other.height &&
               y + height > other.y;
    }

    Rect Intersect(const Rect& other) const {
        float new_x = std::max(x, other.x);
        float new_y = std::max(y, other.y);
        float new_right = std::min(Right(), other.Right());
        float new_bottom = std::min(Bottom(), other.Bottom());

        if (new_x < new_right && new_y < new_bottom) {
            return Rect(new_x, new_y, new_right - new_x, new_bottom - new_y);
        }
        return Rect(); // Empty rect
    }

    Rect Union(const Rect& other) const {
        float new_x = std::min(x, other.x);
        float new_y = std::min(y, other.y);
        float new_right = std::max(Right(), other.Right());
        float new_bottom = std::max(Bottom(), other.Bottom());

        return Rect(new_x, new_y, new_right - new_x, new_bottom - new_y);
    }
};

/**
 * EdgeInsets - Padding/margins for layout
 */
struct EdgeInsets {
    float left;
    float top;
    float right;
    float bottom;

    EdgeInsets() : left(0), top(0), right(0), bottom(0) {}
    EdgeInsets(float all) : left(all), top(all), right(all), bottom(all) {}
    EdgeInsets(float horizontal, float vertical)
        : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
    EdgeInsets(float left, float top, float right, float bottom)
        : left(left), top(top), right(right), bottom(bottom) {}

    Rect Apply(const Rect& rect) const {
        return Rect(
            rect.x + left,
            rect.y + top,
            rect.width - left - right,
            rect.height - top - bottom
        );
    }
};

} // namespace XER::UI::Layout

#endif // XER_UI_LAYOUT_GEOMETRY_H
