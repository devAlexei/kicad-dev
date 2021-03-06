/**
 * @file minimun_spanning_tree.h
 */

#include <vector>

/**
 * @brief The class MIN_SPAN_TREE calculates the rectilinear minimum spanning tree
 * of a set of points (pads usually having the same net)
 * this class is an abstract class because you must provide the function
 *     int  GetWeight( int aItem1, int aItem2 )
 * that calculate the distance between 2 items
 * MIN_SPAN_TREE does not know anything about the actual items to link
 * by the tree
 */
class MIN_SPAN_TREE
{
protected:
    int m_Size;               /* The number of nodes in the graph
                               */
private:
    std::vector<char> inTree; /* inTree[ii] is a flag set to 1 if the node ii
                               *  is already in the minimum spanning tree; 0 otherwise
                               */
    std::vector<int>  linkedTo; /* linkedTo[ii] holds the index of the node ii would have to be
                                 *  linked to in order to get a distance of d[ii]
                                 * NOTE: linkedTo[0] is the starting point of the tree
                                 * linkedTo[1] is the first linked point to use
                                 * ii and linkedTo[ii] are the 2 ends of an edge in the graph
                                 */
    std::vector<int> distTo;  /* distTo[ii] is the distance between node ii and the minimum spanning
                               * tree;
                               * this is initially infinity (INT_MAX);
                               * if ii is already in the tree, then d[ii] is undefined;
                               * this is just a temporary variable. It's not necessary but speeds
                               * up execution considerably (by a factor of n)
                               */
public:
    MIN_SPAN_TREE();
    void MSP_Init( int aNodesCount );
    void BuildTree();

    int GetWhoTo( int aIdx )
    {
        return linkedTo[aIdx];
    }


    int GetDist( int aIdx )
    {
        return distTo[aIdx];
    }

    /**
     * Function GetWeight
     * calculates the weight between 2 items
     * NOTE: The weight between a node and itself should be 0
     * It is virtual pure, you must provide your GetWeight function
     * @param aItem1 = first item
     * @param aItem2 = other item
     * @return the weight between items ( usually the distance )
     */
    virtual int  GetWeight( int aItem1, int aItem2 ) = 0;

private:

    /**
     * Function updateDistances
     *   should be called immediately after target is added to the tree;
     *   updates d so that the values are correct (goes through target's
     *   neighbours making sure that the distances between them and the tree
     *   are indeed minimum)
     * @param aTarget = index of curr item
     */
    void updateDistances( int aTarget );

};
