#ifndef CGAL_CLASSIFICATION_POINT_SET_NEIGHBORHOOD_H
#define CGAL_CLASSIFICATION_POINT_SET_NEIGHBORHOOD_H

#include <vector>

#include <boost/iterator/counting_iterator.hpp>

#include <CGAL/Search_traits_3.h>
#include <CGAL/Fuzzy_sphere.h>
#include <CGAL/Orthogonal_k_neighbor_search.h>
#include <CGAL/Default_diagonalize_traits.h>
#include <CGAL/centroid.h>
#include <CGAL/grid_simplify_point_set.h>

#include <CGAL/Classification/Image.h>

namespace CGAL {

namespace Classification {

  /*!
    \ingroup PkgClassificationDataStructures

    \brief Class that precomputes spatial searching structures for an
    input point set and gives easy access to local neighborhoods of
    points.

    It allows the user to generate models of `NeighborQuery` based on
    a fixed range neighborhood or on a fixed K number of neighbors. In
    addition, the spatial searching structures can be computed on a
    simplified version of the point set to allow for neighbor queries
    at a higher scale.

    \tparam Kernel is a model of \cgal Kernel.
    \tparam Range range of items, model of `ConstRange`. Its iterator type
    is `RandomAccessIterator`.
    \tparam PointMap model of `ReadablePropertyMap` whose key
    type is the value type of the iterator of `Range` and value type
    is `Point_3<Kernel>`.
  */
template <typename Kernel, typename Range, typename PointMap>
class Point_set_neighborhood
{
  
  typedef typename Kernel::FT FT;
  typedef typename Kernel::Point_3 Point;
  
  class My_point_property_map{
    const Range* input;
    PointMap point_map;
    
  public:
    typedef Point value_type;
    typedef const value_type& reference;
    typedef std::size_t key_type;
    typedef boost::lvalue_property_map_tag category;
    My_point_property_map () { }
    My_point_property_map (const Range *input, PointMap point_map)
      : input (input), point_map (point_map) { }
    reference operator[] (key_type k) const { return get(point_map, *(input->begin()+k)); }
    friend inline reference get (const My_point_property_map& ppmap, key_type i) 
    { return ppmap[i]; }
  };

  typedef Search_traits_3<Kernel> SearchTraits_3;
  typedef Search_traits_adapter <std::size_t, My_point_property_map, SearchTraits_3> Search_traits;
  typedef Sliding_midpoint<Search_traits> Splitter;
  typedef Distance_adapter<std::size_t, My_point_property_map, Euclidean_distance<SearchTraits_3> > Distance;
  typedef Kd_tree<Search_traits, Splitter, Tag_true> Tree;
  typedef Fuzzy_sphere<Search_traits> Sphere;
  typedef Orthogonal_k_neighbor_search<Search_traits, Distance, Splitter, Tree> Knn;


  Tree* m_tree;
  Distance m_distance;

  std::vector<std::vector<std::size_t> > m_precomputed_neighbors;
  
public:

  /*!
    Functor that computes the neighborhood of an input point with a
    fixed number of neighbors.

    \cgalModels NeighborQuery
  */
  class K_neighbor_query
  {
  public:
    typedef Point_set_neighborhood::Point value_type; ///<
  private:
    const Point_set_neighborhood& neighborhood;
    std::size_t k;
  public:
    /*!
      \brief Constructs a K neighbor query object.
      \param neighborhood The point set neighborhood structure.
      \param k The number of neighbors per query.
     */
    K_neighbor_query (const Point_set_neighborhood& neighborhood, std::size_t k)
      : neighborhood (neighborhood), k(k) { }

    /// \cond SKIP_IN_MANUAL
    template <typename OutputIterator>
    void operator() (const value_type& query, OutputIterator output) const
    {
      neighborhood.k_neighbors (query, k, output);
    }
    /// \endcond
  };

  /*!
    Functor that computes the neighborhood of an input point defined
    as the points lying in a sphere of fixed radius centered at the
    input point.

    \cgalModels NeighborQuery
  */
  class Range_neighbor_query
  {
  public:
    typedef Point_set_neighborhood::Point value_type; ///<
  private:
    const Point_set_neighborhood& neighborhood;
    double radius;
  public:
    /*!
      \brief Constructs a range neighbor query object.
      \param neighborhood The point set neighborhood structure.
      \param radius The radius of the neighbor query.
    */
    Range_neighbor_query (const Point_set_neighborhood& neighborhood, double radius)
      : neighborhood (neighborhood), radius(radius) { }

    /// \cond SKIP_IN_MANUAL
    template <typename OutputIterator>
    void operator() (const value_type& query, OutputIterator output) const
    {
      neighborhood.range_neighbors (query, radius, output);
    }
    /// \endcond
  };

  friend class K_neighbor_query;
  friend class Range_neighbor_query;

    /// \cond SKIP_IN_MANUAL
  Point_set_neighborhood () : m_tree (NULL) { }
  /// \endcond

  /*!
    \brief Constructs a neighborhood object based on the input range.

    \param input input range.
    \param point_map property map to access the input points.
  */
  Point_set_neighborhood (const Range& input,
                          PointMap point_map)
    : m_tree (NULL)
  {
    My_point_property_map pmap (&input, point_map);
    m_tree = new Tree (boost::counting_iterator<std::size_t> (0),
                       boost::counting_iterator<std::size_t> (input.size()),
                       Splitter(),
                       Search_traits (pmap));
    m_distance = Distance (pmap);
    m_tree->build();
  }

  /*!
    \brief Constructs a simplified neighborhood object based on the input range.

    This method first computes a simplified version of the input point
    set by voxelization: a 3D grid is defined and for each subset
    present in one cell, only the point closest to the centroid of
    this subset is used.

    \param input input range.
    \param point_map property map to access the input points.
    \param voxel_size size of the cells of the 3D grid used for simplification.
  */
  Point_set_neighborhood (const Range& input,
                          PointMap point_map,
                          double voxel_size)
    : m_tree (NULL)
  {
    // First, simplify
    std::vector<std::size_t> indices (input.size());
    for (std::size_t i = 0; i < indices.size(); ++ i)
      indices[i] = i;
    My_point_property_map pmap (&input, point_map);

    voxelize_point_set(indices, pmap, voxel_size);
    
    m_tree = new Tree (indices.begin(), indices.end(),
                       Splitter(),
                       Search_traits (pmap));
    m_distance = Distance (pmap);
    m_tree->build();
  }

  /// \cond SKIP_IN_MANUAL
  ~Point_set_neighborhood ()
  {
    if (m_tree != NULL)
      delete m_tree;
  }
  /// \endcond

  /*!
    \brief Returns a neighbor query object with fixed number of neighbors `k`.
  */
  K_neighbor_query k_neighbor_query (const std::size_t k) const
  {
    return K_neighbor_query (*this, k);
  }

  /*!
    \brief Returns a neighbor query object with fixed radius `radius`.
  */
  Range_neighbor_query range_neighbor_query (const double radius) const
  {
    return Range_neighbor_query (*this, radius);
  }

private:

  template <typename OutputIterator>
  void range_neighbors (const Point& query, const FT radius_neighbors, OutputIterator output) const
  {
    CGAL_assertion (m_tree != NULL);
    Sphere fs (query, radius_neighbors, 0, m_tree->traits());
    m_tree->search (output, fs);
  }

  template <typename OutputIterator>
  void k_neighbors (const Point& query, const std::size_t k, OutputIterator output) const
  {
    CGAL_assertion (m_tree != NULL);
    Knn search (*m_tree, query, (unsigned int)k, 0, true, m_distance);
    for (typename Knn::iterator it = search.begin(); it != search.end(); ++ it)
      *(output ++) = it->first;
  }

  /// \cond SKIP_IN_MANUAL
  template <typename Map>
  void voxelize_point_set (std::vector<std::size_t>& indices, Map point_map,
                           double voxel_size)
  {
    std::map<Point, std::vector<std::size_t> > grid;

    for (std::size_t i = 0; i < indices.size(); ++ i)
      {
        const Point& p = get(point_map, indices[i]);
        Point ref (std::floor(p.x() / voxel_size),
                   std::floor(p.y() / voxel_size),
                   std::floor(p.z() / voxel_size));
        typename std::map<Point, std::vector<std::size_t> >::iterator it;
        boost::tie (it, boost::tuples::ignore)
          = grid.insert (std::make_pair (ref, std::vector<std::size_t>()));
        it->second.push_back (indices[i]);
      }
    indices.clear();
    for (typename std::map<Point, std::vector<std::size_t> >::iterator
           it = grid.begin(); it != grid.end(); ++ it)
      {
        const std::vector<std::size_t>& pts = it->second;
        Point centroid = CGAL::centroid (boost::make_transform_iterator
                                         (pts.begin(),
                                          CGAL::Property_map_to_unary_function<Map>(point_map)),
                                         boost::make_transform_iterator
                                         (pts.end(),
                                          CGAL::Property_map_to_unary_function<Map>(point_map)));
        std::size_t chosen = 0;
        double min_dist = (std::numeric_limits<double>::max)();
        for (std::size_t i = 0; i < pts.size(); ++ i)
          {
            double dist = CGAL::squared_distance(get(point_map, pts[i]), centroid);
            if (dist < min_dist)
              {
                min_dist = dist;
                chosen = pts[i];
              }
          }
        indices.push_back (chosen);
      }
  }
  /// \endcond
};
  

}
  
}


#endif // CGAL_CLASSIFICATION_POINT_SET_POINT_SET_NEIGHBORHOOD_H
