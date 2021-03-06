#ifndef __ADIOS_QUERY_H__
#define __ADIOS_QUERY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "adios_read.h"


enum ADIOS_QUERY_METHOD 
{
    ADIOS_QUERY_METHOD_MINMAX   = 0,
    ADIOS_QUERY_METHOD_FASTBIT  = 1,
    ADIOS_QUERY_METHOD_ALACRITY = 2,
    ADIOS_QUERY_METHOD_UNKNOWN  = 3,
    ADIOS_QUERY_METHOD_COUNT = ADIOS_QUERY_METHOD_UNKNOWN
};
    

enum ADIOS_PREDICATE_MODE 
{
    ADIOS_LT = 0,
    ADIOS_LTEQ = 1,
    ADIOS_GT = 2,
    ADIOS_GTEQ = 3,
    ADIOS_EQ = 4,
    ADIOS_NE = 5
};

enum ADIOS_CLAUSE_OP_MODE 
{
    ADIOS_QUERY_OP_AND = 0,
    ADIOS_QUERY_OP_OR  = 1
};

typedef struct {
    char* condition;
    void* queryInternal;

    // keeping start/count to map 1d results from fastbit to N-d

    ADIOS_SELECTION* sel;
    void* dataSlice;

    ADIOS_VARINFO* varinfo;
    char* varName;

    ADIOS_FILE* file;
    enum ADIOS_QUERY_METHOD method;

    enum ADIOS_PREDICATE_MODE predicateOp;
    char* predicateValue;
    uint64_t rawDataSize; // this is the result of dim/start+count

    void* left;
    void* right;
    enum ADIOS_CLAUSE_OP_MODE combineOp;

    int onTimeStep; // dataSlice is obtained with this timeStep 

    uint64_t maxResultsDesired;
    uint64_t resultsReadSoFar;

    int hasParent;
    int deleteSelectionWhenFreed;

    int estimate; // 0 => the query engine does candidate check; 1 => no candidate check, leading to more results
} ADIOS_QUERY;
   

enum ADIOS_QUERY_RESULT_STATUS 
{
    ADIOS_QUERY_RESULT_ERROR = -1,
    ADIOS_QUERY_NO_MORE_RESULTS  = 0,
    ADIOS_QUERY_HAS_MORE_RESULTS = 1
};

typedef struct {
    enum ADIOS_QUERY_METHOD         method_used; 
    enum ADIOS_QUERY_RESULT_STATUS  status;
    int                             nselections; // number of ADIOS_SELECTION entries in returned array
    ADIOS_SELECTION                *selections;  // single array of ADIOS_SELECTION (structs)
    uint64_t                        npoints;     // total number of data points returned in point selections
    /* 
       if result is from: ADIOS_QUERY_RESULT *result = adios_query_evaluate(...)
       then  result->selections[i] is a struct, not a pointer  

       FASTBIT and ALACRITY returns an array of selections, whose type is ADIOS_SELECTION_POINTS
           Points are 1D local offsets in the bounding box returned in result->selection[i].u.points.container.
           Read operations can be performed directly on such selections.
           Number of points that satisfy the query = npoints =
                   = SUM(result->selection[i].u.points.npoints, i=0,..,nselections-1)
           adios_selection_points_1DtoND() can be used to convert 1D offsets to N-D points
           Delete the selection and the result by calling:
              for (i=0; i < result->nselections; i++) {
                  free (result->selections[i].u.points.points);
              }
              free (result->selections);
              free (result);
       MINMAX returns multiple selections, each of them is of type ADIOS_SELECTION_WRITEBLOCK
           Block id of the Nth returned writeblock selection = result->selection[N].u.block.index
           npoints is 0.
           Delete all selections at once then the result itself by calling  
              free (result->selections);
              free (result);

       The extra read function adios_query_read_boundingbox() works on all of these selections.
     */
} ADIOS_QUERY_RESULT;



#ifndef __INCLUDED_FROM_FORTRAN_API__

/* functions */

int adios_query_is_method_available(enum ADIOS_QUERY_METHOD method);

ADIOS_QUERY* adios_query_create (ADIOS_FILE* f, 
                                 ADIOS_SELECTION* queryBoundary,
                                 const char* varName,
                                 enum ADIOS_PREDICATE_MODE queryOp,
                                 const char* value); 


ADIOS_QUERY* adios_query_combine (ADIOS_QUERY* q1, 
                                  enum ADIOS_CLAUSE_OP_MODE combineOp,
                                  ADIOS_QUERY* q2);

/* 
 *  Select a query method manually for a query evaluation. 
 *  If not set by the user, a suitable query method is chosen at evaluation
*/
void adios_query_set_method (ADIOS_QUERY* q, enum ADIOS_QUERY_METHOD method);


/*
 * Estimate the number of hits of the query at "timestep"
 * 
 * 
 * IN:  q               query
 *      timestep        timestep of interest
 *
 * RETURN:  -1 : error
 *          >=0: estimated hits
 *
 */

int64_t adios_query_estimate (ADIOS_QUERY* q, int timeStep);

// obsolete. time_steps for non-streaming files should show up in estimate/evalute
//void adios_query_set_timestep (int timeStep);

/*
 * Evaluate and return result of the query at the "timestep"
 * result will be limited to "batchSize". May need to call Multiple times
 * to get all the result.
 * 
 * IN:  q               query
 *      timestep        timestep of interest
 *      batchSize       max size of results to return of this call
 *      outputBoundary  query results will be mapped to this selection
 *                      outputBoundary must match the selections used when construct the query
 * if NULL, then will use the first selection used in query
 * OUT: queryResult     list of points
 *                      NULL if no result      
 * RETURN:  -1: error
 *           1: if more results to follow, keep calling evaluate() to find out
 *           0: of no more results to fetch 
 *
 */

ADIOS_QUERY_RESULT * adios_query_evaluate (
                         ADIOS_QUERY* q,
                         ADIOS_SELECTION* outputBoundary, // must supply to get results
                         int timestep,
                         uint64_t batchSize // limit on number of blocks/points returned at once
                     );

/*
 * Reading functions
 */

/* Read in data of 'varname' for all selections in 'selections' for timestep
 * and copy them into 'data' which has a bounding box selection.
 * If a data point falls outside the bounding box, it will not be copied.
 * Memory for each query selection is allocated and freed automatically in this function.
 * Memory for 'data' should be allocated by the user and cover the 'bb' boundingbox.
 * Query should be evaluated before calling this function and 'selections' and 'nselections'
 * should come from ADIOS_QUERY_RESULT, although not necessarily all of them.
 * Returns 0 on success, and the adios_errno value on any error.
 */
int adios_query_read_boundingbox (
        ADIOS_FILE *f,
        ADIOS_QUERY *q,
        const char *varname,
        int timestep,
        unsigned int nselections,
        ADIOS_SELECTION *selections,
        ADIOS_SELECTION *bb,
        void *data
   );


void adios_query_free(ADIOS_QUERY* q);

/* conversion from string to query operation enum type */
enum ADIOS_PREDICATE_MODE adios_query_getOp(const char* opStr);


typedef struct
{
    int nmethods; 		// number of available query methods
    char ** name;     	// array of query method names
    enum ADIOS_QUERY_METHOD * methodID; // enum ID of the method in source code
} ADIOS_AVAILABLE_QUERY_METHODS;

/* Provide the names of query methods available in the running application
 */
ADIOS_AVAILABLE_QUERY_METHODS * adios_available_query_methods();

void adios_available_query_methods_free (ADIOS_AVAILABLE_QUERY_METHODS *);


#endif /* __INCLUDED_FROM_FORTRAN_API__ */

#ifdef __cplusplus
}
#endif

#endif /* __ADIOS_QUERY_H__ */
