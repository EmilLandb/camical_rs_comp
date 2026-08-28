/*------------------------------------------------------------------------*/

extern "C" {
#include "aiger.h"
}

/*------------------------------------------------------------------------*/

class Solver {
public:
  Solver () { }
  virtual ~Solver () { }
  virtual void add (int) = 0;
  virtual void assume (int) = 0;
  virtual int solve () = 0;
  virtual int val (int) = 0;
  virtual void freeze (int) { }
  virtual void melt (int) { }
  virtual void statistics () { }
};

/*------------------------------------------------------------------------*/

#include "../cadical/src/cadical.hpp"

class CaDiCaLSolver : public Solver {
  unsigned bound;
  CaDiCaL::Solver cadical;
  void update ();
public:
  CaDiCaLSolver ();
  void add (int lit) { update (); cadical.add (lit); }
  void assume (int lit) { update (); cadical.assume (lit); }
  int solve () { update (); return cadical.solve (); }
  int val (int lit) { update (); return cadical.val (lit); }
  void freeze (int lit) { update (); cadical.freeze (lit); }
  void melt (int lit) { update (); cadical.melt (lit); }
  void statistics () { update (); cadical.statistics (); }
  CaDiCaL::Solver * get () { return &cadical; }
};

/*------------------------------------------------------------------------*/
#ifdef RISS

extern "C" {
#include "librissc.h"
}

class RISSSolver : public Solver {
  void * riss;
public:
  RISSSolver ();
  ~RISSSolver () { riss_destroy (&riss); }
  void add (int lit) { riss_add (riss, lit); }
  void assume (int lit) { riss_assume (riss, lit); }
  int solve () { return riss_sat (riss); }
  int val (int lit) { return riss_deref (riss, lit); }
};

#endif
/*------------------------------------------------------------------------*/
#ifdef CRYPTOMINISAT

#include "cryptominisat.h"

#include <iostream>

extern "C" {
#include <limits.h>
#include <stdlib.h>
}

class CrytoMiniSATSolver : public Solver {
  CMSat::SATSolver cryptominisat;
  std::vector<CMSat::Lit> assumptions;
  std::vector<CMSat::Lit> clause;
  std::vector<CMSat::lbool> model;
  unsigned nvars;
  CMSat::Lit import (int lit) { 
    assert (lit);
    assert (lit != INT_MIN);
    const unsigned idx = abs (lit);
    if (nvars < idx) {
      cryptominisat.new_vars (idx - nvars);
      nvars = idx;
    }
    assert (cryptominisat.nVars () == nvars);
    const bool inverted = lit < 0;
    return CMSat::Lit (idx-1, inverted);
  }
public:
  CrytoMiniSATSolver ();
  void add (int lit) {
    if (!assumptions.empty ()) assumptions.clear ();
    if (lit) clause.push_back (import (lit));
    else { cryptominisat.add_clause (clause); clause.clear (); }
  }
  void assume (int lit) { assumptions.push_back (import (lit)); }
  int solve () {
    const CMSat::lbool tmp =
      cryptominisat.solve (assumptions.empty () ? 0 : &assumptions);
    int res = (tmp == CMSat::boolToLBool (true) ? 10 : 20);
    if (res == 10) model = cryptominisat.get_model ();
    return res;
  }
  int val (int lit) {
    assert (lit);
    assert (lit != INT_MIN);
    const size_t idx = abs (lit) - 1;
    if (idx >= model.size ()) return -1;
    const CMSat::lbool tmp = model[idx];
    int res = (tmp == CMSat::boolToLBool (true) ? 1 : -1);
    if (lit < 0) res = -res;
    return res;
  }
  void statistics () { cryptominisat.print_stats (); }
};

#endif
/*------------------------------------------------------------------------*/
#ifdef IPASIR

extern "C" {
const char * ipasir_signature ();
void * ipasir_init ();
void ipasir_release (void *);
void ipasir_add (void *, int);
void ipasir_assume (void *, int);
int ipasir_solve (void *);
int ipasir_val (void *, int );
}

class IPASIRSolver : public Solver {
  void * ipasir;
public:
  IPASIRSolver ();
  ~IPASIRSolver () { ipasir_release (ipasir); }
  void add (int lit) { ipasir_add (ipasir, lit); }
  void assume (int lit) { ipasir_assume (ipasir, lit); }
  int solve () { return ipasir_solve (ipasir); }
  int val (int lit) { return ipasir_val (ipasir, lit); }
};

#endif
/*------------------------------------------------------------------------*/

// Standard 'C' header includes.

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cassert>

extern "C" {
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
}

/*------------------------------------------------------------------------*/

// Standard 'C++' header includes.

#include <algorithm>
#include <string>
#include <vector>

/*------------------------------------------------------------------------*/

// Get 'VERSION', 'GITID' and 'COMPILE'.

#include "config.h"

/*------------------------------------------------------------------------*/

#define DEFAULT_MAX_BOUND 1000

using namespace std;

/*------------------------------------------------------------------------*/
#ifdef TRANS
/*------------------------------------------------------------------------*/

struct TransitionRelation :
  public CaDiCaL::ClauseIterator,
  public CaDiCaL::WitnessIterator
{ 
  long nclauses, nwitnesses;

  vector<int> clauses;
  vector<int> extension;

  static void add (vector<int> & a, const vector<int> & b) {
    for (const auto & lit : b) a.push_back (lit);
  }

  static void post (vector<int> & a, const vector<int> & b) {
    add (a, b);
    a.push_back (0);
  }

  static void pre (vector<int> & a, const vector<int> & b) {
    a.push_back (0);
    add (a, b);
  }

  bool clause (const vector<int> & clause) {
    post (clauses, clause);
    nclauses++;
    return true;
  }

  bool witness (const vector<int> & c, const vector<int> & w) {
    pre (extension, w);
    pre (extension, c);
    nwitnesses++;
    return true;
  }

  int max_compressed, max_uncompressed;

  struct { vector<int> compress, uncompress; } table;

  TransitionRelation () :
    nclauses (0), nwitnesses (0),
    max_compressed (0), max_uncompressed (0) { }

  int compress (int lit);

  void compress ();
};

struct Frame {
};

struct Frames {
  int max_var;
  Frames () : max_var (0) { }
};

/*------------------------------------------------------------------------*/
#endif
/*------------------------------------------------------------------------*/

static Solver * solver;
static aiger * model;
static unsigned bad;

static unsigned unrolled;	// bound 'k' unrolled
static long reached = -1;	// bound 'k' where bad state was hit
static long proved = -1;	// bound 'k' bad not reachable proved

/*------------------------------------------------------------------------*/

// Global Options.

static int verbose = 0;
static bool witness = true;

static bool restore = false;
static bool freeze = false;
static bool hybrid = false;
static bool check = false;

static bool incremental = true;
static bool assume = true;
static bool optimize = true;

#ifdef RISS
static bool riss = false;
#endif

#ifdef CRYPTOMINISAT
static bool cryptominisat = false;
#endif

#ifdef IPASIR
static bool ipasir = false;
#endif

/*------------------------------------------------------------------------*/
#ifdef LOGGING

static bool logging = false;

#define LOG(...) \
do { \
  if (!logging) break; \
  log (__VA_ARGS__); \
} while (0)

#else

#define LOG(...)

#endif
/*------------------------------------------------------------------------*/

static void print_usage (int all) {
  printf (
"usage: camical [ <option> ... ] [ <bound> ] <aiger> [ <witness> ]\n"
"\n"
"where <option> is one of the following options\n"
"\n"
"  -h          print common command line options\n"
"  --help      print complete list of command line options\n"
"\n"
"  -n          do not print witness trace\n"
"\n"
"and '<aiger>' is an AIGER model. Witnesses are written to '<stdout>'\n"
"by default or to the file '<witness>' if specified.  The maximum bound\n"
"is by default '%ld' unless explicitly specified with '<bound>'.\n",
  (long) DEFAULT_MAX_BOUND);

  if (all) {
    printf (
#if defined(CRYPTOMINISAT) || defined(RISS) || defined(IPASIR)
"\n"
"You can use an alternative SAT solver instead of the default CaDiCaL:\n"
"\n"
#endif
#ifdef CRYPTOMINISAT
"  --cryptominisat=<bool>   use CryptoMiniSAT\n"
#endif
#ifdef RISS
"  --riss=<bool>            use RISS\n"
#endif
#ifdef IPASIR
"  --ipasir=<bool>          use IPASIR (actually '%s')\n"
#endif
"\n"
"The '<option>' can also be one of the following less common options\n"
"\n"
"  -v          increase verbosity (also for SAT cadical)\n"
#ifdef LOGGING
"  -l          set logging option for for debugging purposes\n"
#endif
"\n"
"  --config    print configuration and exit\n"
"  --version   print version and exit\n"
"\n"
"  --restore=<bool>        default is to restore clauses [true]\n"
"  --freeze=<bool>         freeze and melt instead of restoring [false]\n"
"  --hybrid=<bool>         hybrid freezing and restoring [false]\n"
"\n"
"  --check=<bool>          check frozen semantics for '--freeze' [%s]\n"
"\n"
"  --assume=<bool>         assume good states at lower bounds [true]\n"
"  --incremental=<bool>    incremental solving [true]\n"
"  --optimize=<bool>       optimize encoding [true]\n",
#ifdef IPASIR
  ipasir_signature (),
#endif
  check ? "true" : "false");
    printf (
"\n"
"or one of the following internal options passed to the SAT cadical\n"
"\n");
    CaDiCaL::Solver::usage ();
  }
  fflush (stdout);
}

/*------------------------------------------------------------------------*/

static void error (const char * type, 
                   const char * fmt, va_list & ap) {
  fflush (stdout);
  const bool terminal = isatty (2);
  if (terminal) fputs ("\033[1m", stderr);
  fputs ("camical: ", stderr);
  if (terminal) fputs ("\033[1;31m", stderr);
  fprintf (stderr, "%s: ", type);
  if (terminal) fputs ("\033[0m", stderr);
  vfprintf (stderr, fmt, ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void die (const char * fmt, ...) {
  va_list ap;
  va_start (ap, fmt);
  error ("error", fmt, ap);
  va_end (ap);
}

static void parse_error (const char * fmt, ...) {
  va_list ap;
  va_start (ap, fmt);
  error ("parse error", fmt, ap);
  va_end (ap);
}

static void fatal (const char * fmt, ...) {
  va_list ap;
  va_start (ap, fmt);
  error ("fatal error", fmt, ap);
  va_end (ap);
}

static bool terminal;

static void msg (int level, const char * fmt, ...) {
  if (level > verbose) return;
  fputs ("camical ", stdout);
  if (terminal) fputs ("\033[1;96m", stdout);
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  if (terminal) fputs ("\033[0m", stdout);
  fputc ('\n', stdout);
  fflush (stdout);
}

#ifdef LOGGING

static void log (const char * fmt, ...) {
  fputs ("camical ", stdout);
  if (terminal) fputs ("\033[0;36m", stdout);
  fputs ("LOG @ ", stdout);
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  if (terminal) fputs ("\033[0m", stdout);
  fputc ('\n', stdout);
  fflush (stdout);
}

#endif

/*------------------------------------------------------------------------*/
#ifdef RISS

RISSSolver::RISSSolver () {
  riss = riss_init ();
  msg (0, "initialized %s", riss_signature ());
}

#endif
/*------------------------------------------------------------------------*/
#ifdef CRYPTOMINISAT

CrytoMiniSATSolver::CrytoMiniSATSolver () : nvars (0) {
  cryptominisat.set_verbosity (1);
  msg (0, "initialized CryptoMiniSAT %s", cryptominisat.get_version ());
}

#endif
/*------------------------------------------------------------------------*/
#ifdef IPASIR

IPASIRSolver::IPASIRSolver () {
  ipasir = ipasir_init ();
  msg (0, "initialized IPASIR %s", ipasir_signature ());
}
#endif
/*------------------------------------------------------------------------*/

// Satistics.

static double solving;
static double simulation;
static double encoding;

#ifdef TRANS
static double simplifying;
#endif

static double percent (double a, double b) { return b ? 100.0 * a / b : 0; }

static double process_time () {
  struct rusage u;
  double res;
  if (getrusage (RUSAGE_SELF, &u)) return 0;
  res = u.ru_utime.tv_sec + 1e-6 * u.ru_utime.tv_usec;
  res += u.ru_stime.tv_sec + 1e-6 * u.ru_stime.tv_usec;
  return res;
}

static size_t maximum_resident_set_size () {
  struct rusage u;
  if (getrusage (RUSAGE_SELF, &u)) return 0;
  return ((size_t) u.ru_maxrss) << 10;
}

static double started;
static double * running;

static void start (double & timer) {
  assert (!running);
  started = process_time ();
  running = &timer;
}

static double stop () {
  assert (running);
  double current = process_time ();
  double delta = current - started;
  started = current;
  *running += delta;
  running = 0;
  return delta;
}

/*------------------------------------------------------------------------*/

enum Input {
  AND = 0,
  INPUT = 1,
  LATCH = 2,
};

struct AIG {
  int code;             // CNF integer literal ...
  unsigned valid;       // ... valid until this time.
  Input input;          // input or AND gate
  bool mark;		// for resetting code
  bool optimized;       // consider as XOR or MUX during encoding
  unsigned char ref;    // saturating reference counter
  unsigned id;          // the global AIG id
  unsigned idx;         // input index
  unsigned lit;         // aiger literal
  unsigned time;        // time of input
  unsigned hash;        // size independent hash value 
  AIG * child[2];       // children of this node
  AIG * next;           // collision chain
};

static AIG ** aigs;
static unsigned size_aigs;
static unsigned num_aigs;
static unsigned aig_ids;
static long aig_searches;
static long aig_collisions;

#define TRUE_AIG ((AIG*) 1)
#define FALSE_AIG ((AIG*) 0)

static bool is_constant_aig (AIG * a) {
  return a == TRUE_AIG || a == FALSE_AIG;
}

static bool is_signed_aig (const AIG * a) { return 1 & (size_t) a; }

static AIG * not_aig (AIG * & a) { return (AIG*) (1 ^ (size_t) a); }

static AIG * strip_aig (AIG * & a) {
  return (AIG *) (~(size_t)1 & ((size_t) a));
}

struct less_aigs {
  bool operator () (AIG * a, AIG * b) const {
    if (a == b) return false;
    if (a == FALSE_AIG) return true;
    if (a == not_aig (b)) return is_signed_aig (a);
    if (a == TRUE_AIG) return true;
    const AIG * c = strip_aig (a);
    const AIG * d = strip_aig (b);
    return c->id < d->id;
  }
};

static unsigned hash_aig (AIG * a) {
  AIG * b = strip_aig (a);
  unsigned res = b ? b->hash : 0;
  if (a != b) res = ~res;
  return res;
}

static unsigned hash_input (unsigned idx, unsigned time) {
  unsigned res =  1 + 204290201u * idx;
           res += 3 + 501793109u * time;
  return res;
}

static unsigned hash_aigs (AIG * a, AIG * b) {
  unsigned res  = 5 + hash_aig (a) * 301739539u;
           res += 7 + hash_aig (b) * 102583339u;
  return res;
}

static unsigned reduce_hash_aig (unsigned hash, unsigned size) {
  unsigned shift = 16, res = hash;
  while ((1u << shift) > size) {
    res ^= res >> shift;
    shift >>= 1;
  }
  res &= size - 1;
  assert (res < size);
  return res;
}

static AIG * copy_aig (AIG * a) {
  AIG * stripped = strip_aig (a);
  if (!stripped) return a;
  if (stripped->ref < 255) stripped->ref++;
  return a;
}

static void enlarge_aigs () {
  assert (size_aigs < (1u<<31));
  unsigned new_size_aigs = size_aigs ? 2*size_aigs : 1;
  AIG ** new_aigs = (AIG**) calloc (new_size_aigs, sizeof *new_aigs);
  for (unsigned i = 0; i < size_aigs; i++) {
    for (AIG * a = aigs[i], * next; a; a = next) {
      next = a->next;
      unsigned h = reduce_hash_aig (a->hash, new_size_aigs);
      a->next = new_aigs[h];
      new_aigs[h] = a;
    }
  }
  free (aigs);
  aigs = new_aigs;
  size_aigs = new_size_aigs;
  msg (1, "enlarged AIG table to %u", size_aigs);
}

static AIG * new_aig (Input input,
                      unsigned idx, unsigned time,
		      AIG * a, AIG * b,
		      unsigned lit)
{
  if (num_aigs >= size_aigs) enlarge_aigs ();
  unsigned hash;
  if (input) hash = hash_input (idx, time);
  else {

RESTART:

    // Standard one-level optimization.
    //
    if (a == FALSE_AIG) return FALSE_AIG;
    if (b == FALSE_AIG) return FALSE_AIG;
    if (a == TRUE_AIG) return b;
    if (b == TRUE_AIG) return a;
    if (a == b) return a;
    if (a == not_aig (b)) return FALSE_AIG;

    if (optimize) {

      // The following two-level optimizations are locally decreasing
      // and globally non-increasing.  See our MEMICS'07 paper for more
      // information.
      //
      AIG * not_a = not_aig (a);
      bool s = is_signed_aig (a);
      AIG * u = s ? not_a : a;
      Input i = u->input;
      AIG * c0 = u->child[0];
      AIG * c1 = u->child[1];

      AIG * not_b = not_aig (b);
      bool t = is_signed_aig (b);
      AIG * v = t ? not_b : b;
      Input j = v->input;
      AIG * d0 = v->child[0];
      AIG * d1 = v->child[1];

      // Contradiction (O2).

      if (!s && !i && (c0 == not_b || c1 == not_b))
	return FALSE_AIG;

      if (!t && !j && (d0 == not_a || d1 == not_a))
	return FALSE_AIG;
      
      AIG * not_c0 = not_aig (c0);
      AIG * not_c1 = not_aig (c1);
      AIG * not_d0 = not_aig (d0);
      AIG * not_d1 = not_aig (d1);

      if (!s && !t && !i && !j &&
	  (c0 == not_d0 || c0 == not_d1 || c1 == not_d0 || c1 == not_d1))
	return FALSE_AIG;

      // Optimizations rewriting the requested AND between 'a' and 'b' into
      // sub-expressions of either 'a' or 'b' are problematic if we use
      // freezing, since this might skip frozen next state function literals
      // and try to reuse some of its child literals, which are not frozen
      // (and canot be frozen without freezing everything).  We can continue
      // using these two-level optimizations though as soon we have left the
      // 'old' previously encoded part and only work with new not yet
      // encoded AIGs.  Thus if we are not using freezing or if the nodes
      // 'a' and 'b' are not encoded yet, then we can use these two-level
      // optimizations.  This optimization is switched on in the default
      // 'restore' mode but also in the 'hybrid' mode which only freezes
      // next state functions.
      //
      if (!freeze || (!u->code && !v->code)) {

	  // Subsumption (O2).

	  if (s && !i && (c0 == not_b || c1 == not_b))
	    return b;

	  if (t && !j && (d0 == not_a || d1 == not_a))
	    return a;

	  if (s && !i && !t && !j &&
	      (c0 == not_d0 || c0 == not_d1 || c1 == not_d0 || c1 == not_d1))
	    return b;

	  if (!s && !i && t && !j &&
	      (c0 == not_d0 || c0 == not_d1 || c1 == not_d0 || c1 == not_d1))
	    return a;

	  // Idempotence (O2).

	  if (!s && !i && (c0 == b || c1 == b))
	    return a;

	  if (!t && !j && (d0 == a || d1 == a))
	    return b;

	  // Resolution (O2).

	  if (s && !i && t && !j && c0 == d1 && c1 == not_d0)
	    return not_c0;

	  if (s && !i && t && !j && c0 == d0 && c1 == not_d1)
	    return not_c0;

	  if (s && !i && t && !j && c1 == d1 && c0 == not_d0)
	    return not_c1;

	  if (s && !i && t && !j && c1 == d0 && c0 == not_d1)
	    return not_c1;

	  // Substitution (O3).

	  if (s && !i && c1 == b) { a = not_c0; goto RESTART; }
	  if (s && !i && c0 == b) { a = not_c1; goto RESTART; }
	  if (t && !j && d1 == a) { b = not_d0; goto RESTART; }
	  if (t && !j && d0 == a) { b = not_d1; goto RESTART; }

	  if (s && !i && !t && !j &&
	      (c1 == d1 || c1 == d0)) { a = not_c0; goto RESTART; }
	  if (s && !i && !t && !j &&
	      (c0 == d1 || c0 == d0)) { a = not_c1; goto RESTART; }
	  if (t && !j && !s && !j &&
	      (d1 == c1 || d1 == c0)) { b = not_d0; goto RESTART; }
	  if (t && !j && !s && !j &&
	      (d0 == c1 || d0 == c0)) { b = not_d1; goto RESTART; }

	  // In our MEMICS'07 paper we also describe level four (O4)
	  // rewriting rules (all of type Idempotence), but those sometimes
	  // increase the global number of AIGs, which we confirmed by
	  // our experiments.

      } // end of 'if (!freeze ...'

    } // end of 'if (optimize ...'

    // We reach this point if no simplification has been triggered.
    // Normalize the arguments next.
    //
    if (!less_aigs () (a, b)) swap (a, b);

    hash = hash_aigs (a, b);
  }

  // Search for the node in the hash table.
  //
  aig_searches++;
  unsigned reduced = reduce_hash_aig (hash, size_aigs);
  AIG ** p, * res;
  for (p = aigs + reduced; (res = *p); p = &res->next, aig_collisions++) {
    if (res->hash != hash) continue;
    if (!res->input != !input) continue;
    if (input) {
      if (res->idx != idx) continue;
      if (res->time == time) break;
    } else {
      if (res->child[0] != a) continue;
      if (res->child[1] == b) break;
    }
  }

  // If not found in hash table, generate new one.
  //
  if (!res) {
    *p = res = (AIG*) malloc (sizeof *res);
    res->code = 0;
    res->valid = 0;
    res->input = input;
    res->mark = false;
    res->optimized = false;
    res->ref = 1;
    res->id = aig_ids++;
    res->idx = idx;
    res->lit = lit;
    res->time = time;
    res->hash = hash;
    res->child[0] = copy_aig (a);
    res->child[1] = copy_aig (b);
    res->next = 0;
    num_aigs++;
#ifdef LOGGING
    if (input)
      LOG ("new input AIG[%u] index %u literal %u time %u",
        res->id, res->idx, lit, res->time);
    else
      LOG ("new AND gate AIG[%u] = %sAIG[%u] %sAIG[%u] literal %u time %u",
        res->id,
	is_signed_aig (a) ? "-" : "", strip_aig (a)->id,
	is_signed_aig (b) ? "-" : "", strip_aig (b)->id,
	lit, time);
#endif
  }

  return res;
}

static AIG * new_input (unsigned idx, unsigned time, unsigned lit) {
  assert (idx);
  return new_aig (INPUT, idx, time, 0, 0, lit);
}

#ifdef TRANS

static AIG * new_latch (unsigned idx, unsigned time, unsigned lit) {
  assert (idx);
  return new_aig (LATCH, idx, time, 0, 0, lit);
}

#endif

static AIG * new_and (AIG * a, AIG * b, unsigned time, unsigned lit) {
  return new_aig (AND, 0, time, a, b, lit);
}

static void reset_aigs () {
  LOG ("reset aigs");
  for (unsigned i = 0; i < size_aigs; i++) {
    for (AIG * a = aigs[i], * next; a; a = next) {
      next = a->next;
      free (a);
    }
  }
  free (aigs);
  num_aigs = size_aigs = aig_ids = 0;
  aigs = 0;
}

/*------------------------------------------------------------------------*/

// Symbolic simulation generates AIGs for the unrolled AIGER model.

static AIG invalid_aig_node;
static AIG * invalid_aig = &invalid_aig_node;
static vector<vector<AIG*> > cache;

static AIG * simulated (unsigned lit, unsigned time) {
  if (lit == 0) return FALSE_AIG;
  if (lit == 1) return TRUE_AIG;
  assert (!aiger_is_constant (lit));
  unsigned idx = aiger_lit2var (lit);
  if (time >= cache.size ()) return invalid_aig;
  vector<AIG*> & line  = cache [time];
  if (idx >= line.size ()) return invalid_aig;
  AIG * res = line[idx];
  if (aiger_sign (lit)) res = not_aig (res);
  return res;
}

struct Simulate {                       // symbolic simulation job

  bool prefix;                          // prefix or postfix work?
  unsigned lit;				// AIGER literal
  unsigned time;			// time frame

  Simulate (unsigned l, unsigned t) : prefix (true), lit (l), time (t) { }
};

static vector<Simulate> simulation_stack;

static AIG * simulate (unsigned lit, unsigned time) {

  Simulate root (lit, time);
  assert (simulation_stack.empty ());
  simulation_stack.push_back (root);

  aiger_symbol * s;
  aiger_and * a;
  AIG * res;

  while (!simulation_stack.empty ()) {

    Simulate & cur = simulation_stack.back ();

    if (aiger_is_constant (cur.lit)) { // Skip constants.
      simulation_stack.pop_back ();
      continue;
    }

    unsigned idx = aiger_lit2var (cur.lit);

    // Adapt cache size and check whether 'cur' already simulated.
    //
    {
      while (cur.time >= cache.size ())
        cache.push_back (vector<AIG*> ());
      vector<AIG *> & frame = cache [cur.time];
      while (idx >= frame.size ())
        frame.push_back (invalid_aig);
      AIG * res = frame[idx];
      if (res != invalid_aig) {
        simulation_stack.pop_back ();
        continue;
      }
    }

    if (cur.prefix) { // DFS prefix work: 'cur' seen first time.

      if ((a = aiger_is_and (model, cur.lit))) {                // AND
        cur.prefix = false;
	unsigned time = cur.time;
        simulation_stack.push_back (Simulate (a->rhs1, time));
        simulation_stack.push_back (Simulate (a->rhs0, time));
      } else if ((s = aiger_is_input (model, cur.lit))) {       // input
        assert (s->lit == aiger_strip (cur.lit));
	unsigned var = aiger_lit2var (s->lit);
	assert (var == 1 + (s - model->inputs));
        res = new_input (var, cur.time, s->lit);
        cache[cur.time][idx] = res;
        simulation_stack.pop_back ();
      } else {                                                  // latch
        s = aiger_is_latch (model, cur.lit);
        assert (s);
        if (cur.time) {                                         // next
          cur.prefix = false;
          simulation_stack.push_back (Simulate (s->next, cur.time-1));
        } else {                                                // initial
          if (s->reset == aiger_false) res = FALSE_AIG;
          else if (s->reset == aiger_true) res = TRUE_AIG;
          else {
            assert (s->reset == s->lit);
	    assert (s->lit == aiger_strip (cur.lit));
            unsigned var = aiger_lit2var (s->lit);
            assert (var == 1 + model->num_inputs + (s - model->latches));
            res = new_input (var, time, s->lit);
          }
          cache[0][idx] = res;
          simulation_stack.pop_back ();
        }
      }

    } else { // DFS postfix work: all children simulated.

      if ((a = aiger_is_and (model, cur.lit))) {                // AND
        AIG * l = simulated (a->rhs0, cur.time);
        AIG * r = simulated (a->rhs1, cur.time);
        assert (l != invalid_aig);
        assert (r != invalid_aig);
        res = new_and (l, r, cur.time, a->lhs);
      } else {                                                  // next
        s = aiger_is_latch (model, cur.lit);
        assert (s);
        assert (cur.time);
        res = simulated (s->next, cur.time-1);
        assert (res != invalid_aig);
      }

      // Insert in cache.
      //
      idx = aiger_lit2var (cur.lit);   // previous 'idx' invalid
      cache[cur.time][idx] = res;      // 'frame' would be invalid
      simulation_stack.pop_back ();
    }
  }

  res = simulated (root.lit, root.time);
  assert (res != invalid_aig);

  return res;
}

#ifdef TRANS

static void reset_cache () {
  LOG ("reset simulation cache");
  cache.clear ();
}

#endif

/*------------------------------------------------------------------------*/

// Icremental Tseitin encoding of AIGs into  CNF.

static int true_var;
static int variables;
static long clauses;

static int max_variables;
static long max_clauses;

static void unit (int a) {
  solver->add (a);
  solver->add (0);
  clauses++;
}

static void binary (int a, int b) {
  solver->add (a);
  solver->add (b);
  solver->add (0);
  clauses++;
}

static void ternary (int a, int b, int c) {
  solver->add (a);
  solver->add (b);
  solver->add (c);
  solver->add (0);
  clauses++;
}

static int encoded (AIG * a) {
  AIG * stripped = strip_aig (a);
  int res;
  if (!stripped) res = -true_var;
  else res = stripped->code;
  if (is_signed_aig (a)) res = -res;
  return res;
}

struct Encode {
  bool prefix;
  AIG * aig;
  Encode (AIG * a) : prefix (true), aig (a) { }
};

static vector<Encode> encoding_stack;

static int xorsuccess;

static bool is_xor_aig (AIG * a, AIG * & left, AIG * & right) {
  if (!optimize) return false;
  assert (!is_signed_aig (a));
  AIG * c0 = a->child[0];
  AIG * c1 = a->child[1];
  if (!is_signed_aig (c0)) return false;
  if (!is_signed_aig (c1)) return false;
  c0 = strip_aig (c0);
  c1 = strip_aig (c1);
  if (!a->optimized && c0->ref > 1) return false;
  if (!a->optimized && c1->ref > 1) return false;
  AIG * c00 = c0->child[0];
  AIG * c01 = c0->child[1];
  AIG * c10 = c1->child[0];
  AIG * c11 = c1->child[1];
  AIG * not_c10 = not_aig (c10);
  AIG * not_c11 = not_aig (c11);
  if ((c00 != not_c10 || c01 != not_c11) &&
      (c00 != not_c11 || c01 != not_c10)) return false;
  left = c00;
  right = c01;
  assert (less_aigs () (left, right));
  a->optimized = true;
  xorsuccess++;
  return true;
}

static bool push_xor_children_on_encoding_stack (AIG * a) {
  AIG * l, * r;
  if (!is_xor_aig (a, l, r)) return false;
  encoding_stack.push_back (r);
  encoding_stack.push_back (l);
  return true;
}

static int muxsuccess;

static bool is_mux_aig (AIG * a, AIG * & c, AIG * & t, AIG * & e) {
  if (!optimize) return false;
  assert (!is_signed_aig (a));
  AIG * c0 = a->child[0];
  AIG * c1 = a->child[1];
  if (!is_signed_aig (c0)) return false;
  if (!is_signed_aig (c1)) return false;
  c0 = strip_aig (c0);
  c1 = strip_aig (c1);
  if (!a->optimized && c0->ref > 1) return false;
  if (!a->optimized && c1->ref > 1) return false;
  AIG * c00 = not_aig (c0->child[0]);
  AIG * c01 = not_aig (c0->child[1]);
  AIG * c10 = not_aig (c1->child[0]);
  AIG * c11 = not_aig (c1->child[1]);
  AIG * not_c10 = not_aig (c10);
  AIG * not_c11 = not_aig (c11);
       if (c00 == not_c10) c=c00, t=c11, e=c01; // (c00|c01)&(!c00| c11)
  else if (c00 == not_c11) c=c00, t=c10, e=c01; // (c00|c01)&( c10|!c00)
  else if (c01 == not_c10) c=c01, t=c11, e=c00; // (c00|c01)&(!c01| c11)
  else if (c01 == not_c11) c=c01, t=c10, e=c00; // (c00|c01)&( c10|!c01)
  else return false;
  muxsuccess++;
  a->optimized = true;
  return true;
}

static vector<AIG*> children;

static bool push_mux_children_on_encoding_stack (AIG * a) {
  AIG * c, * t, * e;
  if (!is_mux_aig (a, c, t, e)) return false;
  assert (children.empty ());
  children.push_back (c);
  children.push_back (t);
  children.push_back (e);
  sort (children.begin (), children.end (), less_aigs ());
  while (!children.empty ()) {
    encoding_stack.push_back (children.back ());
    children.pop_back ();
  }
  return true;
}

static vector<AIG*> work;
static vector<AIG*> marked;

static void reset_marked () {
  while (!marked.empty ()) {
    AIG * a = marked.back ();
    marked.pop_back ();
    assert (a->mark);
    a->mark = false;
  }
}

static bool get_and_children (AIG * root) {
  assert (!is_signed_aig (root));
  assert (!root->input);
  if (!optimize) {
    children.push_back (root->child[0]);
    children.push_back (root->child[1]);
    return true;
  }
  assert (work.empty ());
  assert (marked.empty ());
  assert (children.empty ());
  work.push_back (root->child[1]);
  work.push_back (root->child[0]);
  while (!work.empty ()) {
    AIG * a = work.back ();
    work.pop_back ();
    if (is_signed_aig (a) || a->ref > 1 || a->input) children.push_back (a);
    else if (!a->mark) {
      work.push_back (a->child[1]);
      work.push_back (a->child[0]);
      marked.push_back (a);
      a->mark = true;
    }
  }
  reset_marked ();
  sort (children.begin (), children.end (), less_aigs ());
  const vector<AIG*>::const_iterator end_of_children = children.end ();
  vector<AIG*>::iterator j = children.begin ();
  vector<AIG*>::const_iterator i;
  AIG * prev = 0;
  bool inconsistent = false;
  for (i = j; i != end_of_children; i++) {
    AIG * a = *i;
    if (a == prev) continue;
    if (a == not_aig (prev)) { inconsistent = true; break; }
    prev = *j++ = a;
  }
  if (inconsistent) children.clear ();
  else children.resize (j - children.begin ());
  return !inconsistent;
}

static bool push_and_children_on_encoding_stack (AIG * root) {
  if (!get_and_children (root)) return false;
  while (!children.empty ()) {
    AIG * a = children.back ();
    children.pop_back ();
    encoding_stack.push_back (a);
  }
  return true;
}

static int encode_false () {
  if (!true_var) {
    true_var = ++variables;
    LOG ("encode TRUE code %d", true_var);
    if (freeze || hybrid) solver->freeze (true_var);
    unit (true_var);
  }
  return -true_var;
}

static int encode (AIG * a) {

  Encode root (a);
  assert (encoding_stack.empty ());
  encoding_stack.push_back (root);
  int res;

  while (!encoding_stack.empty ()) {

    Encode & cur = encoding_stack.back ();

    AIG * s = strip_aig (cur.aig);

    if (!s) {
      (void) encode_false ();
      encoding_stack.pop_back ();
#if 0
    } else if (s->code) { 
      // This code does NOT respect 'freeze' semantics and thus should
      // trigger failures in the molten literal checker in CaDiCaL library,
      // which needs to be enabled with '--check'.
#else
    } else if (s->code && (!freeze || s->valid >= unrolled)) {
      // Correct code for 'freeze' semantics.  Should not trigger
      // any fatal mesage in CaDiCaL even if '--check' is set.
#endif
      encoding_stack.pop_back ();
    } else if (cur.prefix) {
      if (freeze && s->code) {
	LOG ("need to reencode AIG[%u] "
	  "purging old code %d valid until %u for literal %u at time %u",
	  s->id, s->code, s->valid, s->lit, s->time);
	if (s->input) fatal ("need to purge Tseitin code of input gate");
	s->code = 0;
	s->valid = 0;
      }
      cur.prefix = false;
      if (s->input) {
	s->code = ++variables;
	s->valid = unrolled;
	LOG ("encoded AIG[%u] INPUT with code %d "
	  "valid until %u for literal %u at time %u",
	  s->id, s->code, unrolled, s->lit, s->time);
	encoding_stack.pop_back ();
      } else if (!push_xor_children_on_encoding_stack (s) &&
                 !push_mux_children_on_encoding_stack (s) &&
                 !push_and_children_on_encoding_stack (s)) {
        s->code = encode_false ();
	s->valid = UINT_MAX;
	LOG ("encoded AIG[%u] FALSE with code %d ",
	  "for literal %u at time %u",
	  s->id, s->code, s->lit, s->time);
	encoding_stack.pop_back ();
      }
    } else {
      assert (!s->input);
      encoding_stack.pop_back ();
      AIG * c0, * c1;
      res = s->code = ++variables;
      s->valid = unrolled;
      if (is_xor_aig (s, c0, c1)) {
	LOG ("encoded AIG[%u] XOR with code %d "
	  "valid until %u for literal %u at time %u",
	  s->id, s->code, unrolled, s->lit, s->time);
	int l = encoded (c0);
	int r = encoded (c1);
	ternary (-l, -r, -res);
	ternary (l, r, -res);
	ternary (-l, r, res);
	ternary (l, -r, res);
      } else {
	AIG * c, * t, * e;
	if (is_mux_aig (s, c, t, e)) {
	  int i = encoded (c);
	  int l = encoded (t);
	  int r = encoded (e);
	  ternary (-i, -l, res);
	  ternary (-i, l, -res);
	  ternary (i, -r, res);
	  ternary (i, r, -res);
	  fprintf (stderr, "%d %d %d\n", i, l, r);
	} else {
	  const bool ok = get_and_children (s);
	  assert (ok), (void) ok;
	  LOG ("encoded AIG[%u] AND with code %d "
	  "valid until %u for literal %u at time %u",
	    s->id, s->code, unrolled, s->lit, s->time);
	  for (size_t i = 0; i < children.size (); i++)
	    binary (encoded (children[i]), -res);
	  for (size_t i = 0; i < children.size (); i++)
	    solver->add (-encoded (children[i]));
	  solver->add (res);
	  solver->add (0);
	  children.clear ();
	}
      }
    }
  }

  res = encoded (root.aig);
  assert (res);
  assert (!(muxsuccess & 1));
  assert (!(xorsuccess & 1));
  return res;
}

/*------------------------------------------------------------------------*/

// When freezing and melting variables we have to freeze the next state
// functions of all latches before calling 'solve' and melt them afterwards.

static void simulate_all_next_state_functions () {
  for (unsigned i = 0; i < model->num_latches; i++) {
    LOG ("simulating next state function of latch %u at time %u",
      i, unrolled);
    (void) simulate (model->latches[i].next, unrolled);
  }
}

static void encode_and_freeze_all_next_state_functions () {
  assert (freeze || hybrid);
  for (unsigned i = 0; i < model->num_latches; i++) {
    AIG * a = simulated (model->latches[i].next, unrolled);
    assert (a != invalid_aig);
    LOG ("encoding next state function of latch %u at time %u",
      i, unrolled);
    if (is_constant_aig (a)) continue;
    int lit = encode (a);
    AIG * s = strip_aig (a);
    LOG ("freezing %sAIG[%u] next state literal %d of latch %u at time %u",
      is_signed_aig (a) ? "-" : "", s->id, lit, i, unrolled);
    solver->freeze (lit);
    if (s->valid <= unrolled) {
      s->valid = unrolled + 1;
      LOG ("promoted AIG[%u] with code %d "
        "to be valid until %u for literal %u at time %u",
	s->id, s->code, s->valid, s->time, s->time);
    }
  }
}

static void melt_all_previous_next_state_aigs () {
  assert (freeze || hybrid);
  LOG ("melting literals of all previous next state function aigs");
  assert (unrolled > 0);
  for (unsigned i = 0; i < model->num_latches; i++) {
    AIG * a = simulated (model->latches[i].next, unrolled-1);
    assert (a != invalid_aig);
    if (is_constant_aig (a)) continue;
    int lit = encoded (a);
    assert (lit);
    LOG ("melting %sAIG[%u] literal %d of "
      "next state function of latch %u at time %u",
      is_signed_aig (a) ? "-" : "", strip_aig (a)->id, lit,
      i, unrolled-1);
    solver->melt (lit);
  }
}

static void reset_encoding () {
  long reset = 0;
  for (unsigned i = 0; i < size_aigs; i++)
    for (AIG * a = aigs[i]; a; a = a->next)
      if (a->code) a->code = 0, a->valid = 0, reset++;
  msg (0, "reset %ld encoded aigs", reset);
  true_var = variables = 0;
}

/*------------------------------------------------------------------------*/

static vector<string> options;

void CaDiCaLSolver::update () {
  if (bound >= unrolled) return;
  string prefix = "cadical[" + to_string (bound = unrolled) + "] ";
  cadical.prefix (prefix.c_str ());
}

CaDiCaLSolver::CaDiCaLSolver () : bound (0) {
  update ();
  if (::check) assert (::freeze), cadical.set ("checkfrozen", 1);
  for (size_t i = 0; i < options.size (); i++)
    cadical.set_long_option (options[i].c_str ());
  msg (0, "initialized CaDiCaL %s", CaDiCaL::Solver::version ());
}

/*------------------------------------------------------------------------*/

static void init_solver () {
#ifdef RISS
  if (riss) solver = new RISSSolver ();
  else
#endif
#ifdef CRYPTOMINISAT
  if (cryptominisat) solver = new CrytoMiniSATSolver ();
  else 
#endif
#ifdef IPASIR
  if (ipasir) solver = new IPASIRSolver ();
  else
#endif
  solver = new CaDiCaLSolver ();
}

static void reset_solver () {
  assert (solver);
  delete solver;
  solver = 0;
}

/*------------------------------------------------------------------------*/
#ifdef TRANS
/*------------------------------------------------------------------------*/

static void simulate_transition_relation () {

  start (simulation);

  {
    assert (cache.empty ());
    cache.push_back (vector<AIG*> ());
    vector<AIG *> & frame = cache[0];
    assert (frame.empty ());
    frame.push_back (FALSE_AIG);
  }

  for (unsigned i = 0; i < model->num_inputs; i++) {
    LOG ("simulating input %u", i);
    assert (!cache.empty ());
    vector<AIG *> & frame = cache[0];
    const unsigned lit = model->inputs[i].lit;
    const unsigned var = aiger_lit2var (lit);
    assert (var > 0);
    while (var >= frame.size ())
      frame.push_back (invalid_aig);
    assert (frame[var] == invalid_aig);
    frame[var] = new_input (var, 0, lit);
  }

  for (unsigned i = 0; i < model->num_latches; i++) {
    LOG ("simulating latch %u", i);
    assert (!cache.empty ());
    vector<AIG *> & frame = cache[0];
    const unsigned lit = model->latches[i].lit;
    const unsigned var = aiger_lit2var (lit);
    assert (var > 0);
    while (var >= frame.size ())
      frame.push_back (invalid_aig);
    assert (frame[var] == invalid_aig);
    frame[var] = new_latch (var, 0, lit);
  }

  {
    LOG ("simulating bad state property");
    (void) simulate (bad, 0);
  }

  for (unsigned i = 0; i < model->num_latches; i++) {
    LOG ("simulating next state function of latch %u", i);
    (void) simulate (model->latches[i].next, 0);
  }

  double delta = stop ();
  msg (0, "simulated transitition relation in %.2f seconds", delta);
}

/*------------------------------------------------------------------------*/

static CaDiCaL::Solver * init_cadical () {
  CaDiCaL::Solver * res;
  CaDiCaLSolver * tmp = new CaDiCaLSolver ();
  res = tmp->get ();
  assert (!solver);
  solver = tmp;
  res->prefix ("cadical[simp] ");
  return res;
}

static void encode_transition_relation (CaDiCaL::Solver * cadical) {

  start (encoding);

  {
    AIG * aig = simulated (aiger_true, 0);
    assert (aig != invalid_aig), (void) aig;
    int cnf_lit = encode (TRUE_AIG);
    assert (true_var == cnf_lit);
    assert (true_var == 1);
    (void) cnf_lit;
  }

  for (unsigned i = 0; i < model->num_inputs; i++) {
    LOG ("encoding input %u", i);
    AIG * aig = simulated (model->inputs[i].lit, 0);
    assert (aig != invalid_aig);
    int cnf_lit = encode (aig);
    assert (cnf_lit > 0);
    assert ((unsigned) cnf_lit == i + 2);
    cadical->freeze (cnf_lit);
  }

  for (unsigned i = 0; i < model->num_latches; i++) {
    LOG ("encoding latch %u", i);
    AIG * aig = simulated (model->latches[i].lit, 0);
    assert (aig != invalid_aig);
    int cnf_lit = encode (aig);
    assert (cnf_lit > 0);
    assert ((unsigned) cnf_lit == i + (model->num_inputs + 2));
    cadical->freeze (cnf_lit);
  }

  {
    LOG ("encoding bad state property");
    AIG * aig = simulated (bad, 0);
    assert (aig != invalid_aig);
    int cnf_lit = encode (aig);
    cadical->freeze (cnf_lit);
  }

  for (unsigned i = 0; i < model->num_latches; i++) {
    LOG ("simulating next state function of latch %u", i);
    AIG * aig = simulate (model->latches[i].next, 0);
    int cnf_lit = encode (aig);
    cadical->freeze (cnf_lit);
  }

  double delta = stop ();
  msg (0, "encoded transitition relation in %.2f seconds", delta);
}

/*------------------------------------------------------------------------*/

#include "random.hpp"

static void random_inputs (CaDiCaL::Solver * cadical,
                           CaDiCaL::Random & random) {

  for (unsigned i = 0; i < model->num_inputs; i++) {
    AIG * aig = simulated (model->inputs[i].lit, 0);
    assert (aig != invalid_aig);
    int idx = encoded (aig);
    assert (idx > 0);
    const int phase = random.generate_bool () ? -1 : 1;
    const int lit = phase * idx;
    LOG ("randomized assignment of input %u to %d", i, (phase < 1));
    cadical->assume (lit);
  }

}

static void random_latches (CaDiCaL::Solver * cadical,
                           CaDiCaL::Random & random) {

  for (unsigned i = 0; i < model->num_latches; i++) {
    AIG * aig = simulated (model->latches[i].lit, 0);
    assert (aig != invalid_aig);
    int idx = encoded (aig);
    assert (idx > 0);
    const int phase = random.generate_bool () ? -1 : 1;
    const int lit = phase * idx;
    LOG ("randomized assignment of latch %u to %d", i, (phase < 1));
    cadical->assume (lit);
  }

}

static void melting_inputs (CaDiCaL::Solver * cadical) {

  for (unsigned i = 0; i < model->num_inputs; i++) {
    AIG * aig = simulated (model->inputs[i].lit, 0);
    assert (aig != invalid_aig);
    int idx = encoded (aig);
    assert (idx > 0);
    LOG ("melting input %u", i);
    cadical->melt (idx);
  }

}

/*------------------------------------------------------------------------*/

static TransitionRelation * simulate_and_encode_transition_relation () {
  simulate_transition_relation ();
  CaDiCaL::Solver * cadical = init_cadical ();
  encode_transition_relation (cadical);
  start (simplifying);
  CaDiCaL::Random random (42);
  for (int i = 1; i <= 50; i++) {
    msg (0, "");
    msg (0, "simulation and simplification round %d", i);
    msg (0, "");
    cadical->limit ("conflicts", 10000);
    random_inputs (cadical, random);
    random_latches (cadical, random);
    (void) cadical->solve ();
    (void) cadical->simplify (1);
  }
  melting_inputs (cadical);
  msg (0, "");
  msg (0, "final simplification after melting inputs");
  msg (0, "");
  cadical->simplify (10);
  double delta = stop ();
  msg (0, "");
  msg (0, "simplified transition relation in %.2f seconds", delta);
  msg (0, "");
  solver->statistics ();
  reset_encoding ();
  reset_cache ();
  reset_aigs ();
  TransitionRelation * T = new TransitionRelation;
  cadical->traverse_clauses (*T);
  cadical->traverse_witnesses_backward (*T); // REVIEW: changed this to backward
  delete solver;
  solver = 0;
  msg (0, "extracted %ld clauses %ld witnesses",
    T->nclauses, T->nwitnesses);
  T->compress ();
  msg (0, "compressed %d original to %d variables %.0f%%",
    T->max_uncompressed, T->max_compressed,
    percent (T->max_compressed, T->max_uncompressed));
  msg (0, "");
  return T;
}

/*------------------------------------------------------------------------*/

int TransitionRelation::compress (int lit) {
  if (!lit) return 0;
  assert (lit);
  assert (lit != INT_MIN);
  int idx = abs (lit);
  if (idx > max_uncompressed) max_uncompressed = idx;
  while (table.compress.size () <= (size_t) idx)
    table.compress.push_back (0);
  int res = table.compress[idx];
  if (!res) {
    assert (max_compressed < INT_MAX);
    res = ++max_compressed;
    LOG ("uncompressed %d compressed as %d", idx, res);
    table.compress[idx] = res;
    table.uncompress.push_back (idx);
    assert (table.uncompress.size () == (size_t) max_compressed);
  }
  if (lit) res = -res;
  return res;
}

void TransitionRelation::compress () {
  assert (!max_uncompressed);
  for (auto & lit : clauses)
    max_uncompressed = max (max_uncompressed, abs (lit));
  for (auto & lit : extension)
    max_uncompressed = max (max_uncompressed, abs (lit));
  msg (0, "maximum uncompressed index %d", max_uncompressed);
  bool active[max_uncompressed+1] = { false };
  for (auto & lit : clauses)
    if (lit) active[abs (lit)] = true;
  for (int idx = 1; idx <= max_uncompressed; idx++)
    if (active [idx])
      (void) compress (idx);
}

/*------------------------------------------------------------------------*/
#endif // ifdef TRANS
/*------------------------------------------------------------------------*/

static void stats () {
  double t = process_time ();
  if (unrolled) msg (0, "unrolled until bound %u", unrolled);
  else msg (0, "only initial state at bound 0 checked");
  if (reached < 0) msg (0, "bad state was not reached");
  else msg (0, "bad state reached at bound %ld", reached);
  if (proved < 0) msg (0, "failed to prove that initial state is good");
  else msg (0, "no bad state reachable until bound %ld", proved);
  msg (0, "simulated %ld aigs (%ld searches, %ld collisions %.0f%%)",
    aig_ids, aig_searches, aig_collisions,
    percent (aig_collisions, aig_searches));
  msg (0, "encoded %s%d variables and %ld clauses",
    (incremental ? "" : "maximally "), max_variables, max_clauses);
  msg (0, "maximum resident set size %.1f MB",
    maximum_resident_set_size () / (double)(1<<20));
  msg (0, "simulation time %.2f seconds %.0f%%",
    simulation, percent (simulation, t));
  msg (0, "encoding time %.2f seconds %.0f%%",
    encoding, percent (encoding, t));
#ifdef TRANS
  msg (0, "simplifying time %.2f seconds %.0f%%",
    simplifying, percent (simplifying, t));
#endif
  msg (0, "solving time %.2f seconds %.0f%%",
    solving, percent (solving, t));
  msg (0, "total time %.2f seconds 100%%", t);
}

/*------------------------------------------------------------------------*/

// Signal handling.

static bool caught_signal;
static bool reported_stats;

static void (*old_SIGABRT_handler)(int);
static void (*old_SIGBUS_handler)(int);
static void (*old_SIGINT_handler)(int);
static void (*old_SIGSEGV_handler)(int);
static void (*old_SIGTERM_handler)(int);

static void reset_signal_handlers () {
  (void) signal (SIGABRT, old_SIGABRT_handler);
  (void) signal (SIGBUS, old_SIGBUS_handler);
  (void) signal (SIGINT, old_SIGINT_handler);
  (void) signal (SIGSEGV, old_SIGSEGV_handler);
  (void) signal (SIGTERM, old_SIGTERM_handler);
}

static void catch_signal (int sig) {
  if (!caught_signal) {
    caught_signal = true;
    if (running) stop ();
    if (terminal) msg (0, "\033[1;31mcaught SIGNAL %d\033[0m", sig);
    else msg (0, "caught SIGNAL %d", sig);
  }
  if (!reported_stats) {
    if (solver) solver->statistics ();
    stats ();
    reported_stats = true;
  }
  reset_signal_handlers ();
  if (terminal) msg (0, "\033[1;31mraising SIGNAL %d\033[0m", sig);
  else msg (0, "raising SIGNAL %d", sig);
  if (terminal) fputs ("\033[0m", stdout), fflush (stdout);
  if (isatty (2)) fputs ("\033[0m", stderr), fflush (stderr);
  raise (sig);
}

static void init_signal_handlers () {
  old_SIGABRT_handler = signal (SIGABRT, catch_signal);
  old_SIGBUS_handler = signal (SIGBUS, catch_signal);
  old_SIGINT_handler = signal (SIGINT, catch_signal);
  old_SIGSEGV_handler = signal (SIGSEGV, catch_signal);
  old_SIGTERM_handler = signal (SIGTERM, catch_signal);
}

/*------------------------------------------------------------------------*/

// Print the actual witness trace (initial state and input vectors).

static void print_witness (FILE * file) {
  string line;
  for (unsigned i = 0; i < model->num_latches; i++) {
    char ch;
    aiger_symbol * s = model->latches + i;
         if (s->reset == aiger_false) ch = '0';
    else if (s->reset == aiger_true) ch = '1';
    else {
      assert (s->reset == s->lit);
      AIG * a = simulated (model->latches[i].lit, 0);
      if (a == invalid_aig) ch = 'x';
      else {
        int lit = encoded (a);
        if (!lit) ch = 'x';
        else {
          int val = solver->val (lit);
          assert (val);
          ch = val < 0 ? '0' : '1';
        }
      }
    }
    line.push_back (ch);
  }
  fputs (line.c_str (), file);
  fputc ('\n', file);
  line.clear ();
  for (unsigned i = 0; i <= unrolled; i++) {
    for (unsigned j = 0; j < model->num_inputs; j++) {
      aiger_symbol * s = model->inputs + j;
      AIG * a = simulated (s->lit, i);
      char ch;
      if (a == invalid_aig) ch = 'x';
      else {
        int lit = encoded (a);
        if (!lit) ch = 'x';
        else {
          int val = solver->val (lit);
          assert (val);
	  ch = val < 0 ? '0' : '1';
        }
      }
      line.push_back (ch);
    }
    fputs (line.c_str (), file);
    fputc ('\n', file);
    line.clear ();
  }
}

/*------------------------------------------------------------------------*/

// Helper functions for 'main'.

static bool is_unsigned_integer (const char * str) {
  if (!isdigit (*str++)) return false;
  while (*str)
    if (!isdigit (*str++))
      return false;
  return true;
}

static void print_new_bound () {
  char buffer[80];
  sprintf (buffer, "===== [ bound %u ] ", unrolled);
  int len = strlen (buffer);
  while (len < 70) buffer[len++] = '=';
  buffer[len] = 0;
  msg (0, buffer);
}

/*------------------------------------------------------------------------*/

static bool match (const char * arg,
                   const char * s1, const char * s2 = 0) {
  const char * p = arg, * q = s1;
  while (*q) if (*p++ != *q++) return false;
  if (!(q = s2)) return !*p;
  while (*q) if (*p++ != *q++) return false;
  return !*p;
}


static bool option (const char * arg, const char * name, bool & val) {
  if (arg[0] != '-' || arg[1] != '-') return false;
  else if (match (arg+2, name)) val = true;
  else if (match (arg+2, "no-", name)) val = false;
  else if (match (arg+2, name, "=false")) val = false;
  else if (match (arg+2, name, "=true")) val = true;
  else if (match (arg+2, name, "=0")) val = false;
  else if (match (arg+2, name, "=1")) val = true;
  else return false;
  return true;
}

int main (int argc, char ** argv) { 
  terminal = isatty (1);
  const char * witness_path = 0;
  const char * aiger_path = 0;
  long bound = -1;
  for (int i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) print_usage (false), exit (0);
    else if (!strcmp (argv[i], "--help")) {
      print_usage (true);
      exit (0);
    } else if (!strcmp (argv[i], "--version")) {
      printf ("%s%s\n", VERSION, CaDiCaL::Solver::version ());
      exit (0);
    } else if (!strcmp (argv[i], "--config")) {
      fputs ("Version " VERSION " " GITID "\n", stdout);
      fputs (COMPILE "\n", stdout);
      fputs (BUILD "\n", stdout);
      exit(0);
    }
#ifdef LOGGING
    else if (!strcmp (argv[i], "-l")) logging = true;
#else
    else if (!strcmp (argv[i], "-l"))
      die ("invalid logging option '-l' (compiled without '-DLOGGING')");
#endif
#ifdef CRYPTOMINISAT
    else if (option (argv[i], "cryptominisat", cryptominisat)) { }
#endif
#ifdef RISS
    else if (option (argv[i], "riss", riss)) { }
#endif
#ifdef IPASIR
    else if (option (argv[i], "ipasir", ipasir)) { }
#endif
    else if (option (argv[i], "restore", restore)) { }
    else if (option (argv[i], "freeze", freeze)) { }
    else if (option (argv[i], "hybrid", hybrid)) { }
    else if (option (argv[i], "check", check)) { }
    else if (option (argv[i], "assume", assume)) { }
    else if (option (argv[i], "incremental", incremental)) { }
    else if (option (argv[i], "optimize", optimize)) { }
    else if (!strcmp (argv[i], "-v")) verbose++;
    else if (!strcmp (argv[i], "-n")) witness = false;
    else if (argv[i][0] == '-' && argv[i][1] == '-') {
      if (!CaDiCaL::Solver::is_valid_long_option (argv[i]))
	die ("invalid long option '%s' (try '-h')", argv[i]);
      options.push_back (argv[i]);
    } else if (argv[i][0] == '-')
      die ("invalid short option '%s' (try '-h')", argv[i]);
    else if (is_unsigned_integer (argv[i])) {
      if (bound >= 0)
        die ("two bounds '%ld' and '%s' specified", bound, argv[i]);
      bound = atol (argv[i]);
      if (bound < 0 || bound >= UINT_MAX)
        die ("invalid bound '%s'", argv[i]);
    } else if (witness_path)
      die ("too many files '%s', '%s' and '%s'",
        aiger_path, witness_path, argv[i]);
    else if (aiger_path) witness_path = argv[i];
    else aiger_path = argv[i];
  }

  if (freeze && restore)
    die ("can not combine '--freeze' and '--restore'");
  if (freeze && hybrid)
    die ("can not combine '--freeze' and '--hybrid'");
  if (freeze && !incremental)
    die ("can not combine '--freeze' with '--no-incremental'");
  if (check && !freeze)
    die ("can not use '--check' without '--freeze'");
  if (hybrid && !incremental)
    die ("can not combine '--hybrid' with '--no-incremental'");
  if (hybrid && restore)
    die ("can not combine '--hybrid' with '--restore'");
  if (restore && !incremental)
    die ("can not combine '--restore' with '--no-incremental'");

  if (!aiger_path) die ("AIGER model missing (try '-h')");
  model = aiger_init ();
  const char * err = aiger_open_and_read_from_file (model, aiger_path);
  if (err) parse_error ("%s: %s", aiger_path, err);
  if (model->num_constraints) die ("invariant constraints unsupported");
  if (model->num_justice) die ("justice constraints unsupported");
  if (model->num_fairness) die ("fairness constraints unsupported");
  if (model->num_bad > 1) die ("multiple bad state constraints");
  if (model->num_outputs > 1) die ("multiple outputs");
  if (model->num_outputs && model->num_bad)
    die ("found both output and bad state constraint");
  if (!model->num_outputs && !model->num_bad)
    die ("no output nor bad state constraint found");
  msg (0, "CaMiCaL Model Checker");
  if (terminal) msg (0, "Version \033[1;37m" VERSION "\033[1;96m " GITID);
  else msg (0, "Version " VERSION " " GITID);
  msg (0, COMPILE);
  msg (0, BUILD);
  //CaDiCaL::Solver::configuration (stdout, "cadical "); // not supported anymore?
  if (model->num_outputs) {
    msg (0, "parsed MILOA header 'aig %u %u %u %u %u'",
      model->maxvar,
      model->num_inputs,
      model->num_latches,
      model->num_outputs,
      model->num_ands);
    bad = model->outputs[0].lit;
  } else {
    msg (0, "parsed MILOAB header 'aig %u %u %u %u %u %u'",
      model->maxvar,
      model->num_inputs,
      model->num_latches,
      model->num_outputs,
      model->num_ands,
      model->num_bad);
    bad = model->bad[0].lit;
  }

  if (!aiger_is_reencoded (model)) {
    msg (0, "need to reencode model");
    aiger_reencode (model);
  }

  if (bound < 0) {
    bound = DEFAULT_MAX_BOUND;
    msg (0, "using default maximum bound '%ld'", bound);
  } else {
    msg (0, "maximum bound '%ld' as specified", bound);
  }

  if (!freeze && !hybrid && !restore && incremental)  {
    msg (0,
      "using default incremental restore mode "
      "(no '--freeze', '--hybrid' nor '--no-incremental')");
    restore = true;
  }

  assert (restore + freeze + hybrid + !incremental == 1);

  if (freeze) {
    msg (0, "freezing and melting variables (--freeze)");
    if (check)
      msg (0, "checking classical freezing contract (--check)");
  }
  if (hybrid)
    msg (0, "hybrid combination of freezing and restoring (--hybrid)");
  if (restore) 
    msg (0, "relying on restoring clauses (--restore)");

  if (incremental)
    msg (0, "default incremental solving (--incremental)");
  else if (assume)
    msg (0, "non-incremental solving (--no-incremental) "
      "assuming good states at lower bounds (--assume)");
  else
    msg (0, "non-incremental solving (--no-incremental) "
      "without assuming good states (--no-assume)");

#ifdef LOGGING
  if (logging) {
    options.push_back ("--log");
    verbose = 2;
  }
#endif
  if (verbose == 1) options.push_back ("--verbose=1");
  if (verbose == 2) options.push_back ("--verbose=2");

  init_signal_handlers ();

#ifdef TRANS
  TransitionRelation * T = simulate_and_encode_transition_relation ();
  delete T;
#endif

  int res = 0;

  vector<AIG*> good;

  for (;;) {

    double started = process_time ();

    print_new_bound ();
    if (!solver) init_solver ();

    start (simulation);
    AIG * a = simulate (bad, unrolled);
    if (freeze || hybrid) simulate_all_next_state_functions ();
    good.push_back (not_aig (a));
    stop ();

    start (encoding);
    if (!incremental && assume)
      for (unsigned i = 0; i < unrolled; i++)
	unit (encode (good[i]));
    int lit = encode (a);
    if (freeze | hybrid) {
      encode_and_freeze_all_next_state_functions ();
      if (unrolled) melt_all_previous_next_state_aigs ();
    }
    if (max_variables < variables) max_variables = variables;
    if (max_clauses < clauses) max_clauses = clauses;
    if (!incremental) msg (0, "variables %d, clauses %ld", variables, clauses);
    stop ();

    if (incremental) {
      solver->assume (lit);
      if (::freeze) solver->freeze (lit);
    } else unit (lit);

    start (solving);
    int sat = solver->solve ();
    stop ();

    double delta = process_time () - started;

    msg (0, "");
    msg (0, "solved bound %u after %.2f seconds in %.2f seconds",
      unrolled, process_time (), delta);
    msg (0, "");

    if (sat == 10) { 
      res = 10;
      reached = unrolled;
      break;
    } else {
      assert (sat == 20);
      if (!model->num_latches) { res = 20; break; }
      printf ("u%u\n", unrolled);
      fflush (stdout);
      proved = unrolled;

      if (incremental) {
	unit (-lit);
	if (::freeze) solver->melt (lit);
      }
    }
    if (unrolled == bound) break;

    if (!incremental) {
      solver->statistics ();
      reset_solver ();
      reset_encoding ();
    }

    unrolled++;
  }

  FILE * witness_file;
  if (witness_path) {
    witness_file = fopen (witness_path, "w");
    if (!witness_file)
      die ("failed to write witness to '%s'", witness_path);
    msg (0, "writing witness to '%s'", witness_path);
  } else {
    witness_file = stdout;
    msg (0, "writing witness to '<stdout>'");
  }
  if (res == 10) {
    fprintf (witness_file, "1\n");
    fprintf (witness_file, "b0\n");
    if (witness)  
      fflush (witness_file),
      print_witness (witness_file);
  } else if (res == 20) {
    fprintf (witness_file, "0\n");
    fprintf (witness_file, "b0\n");
  } else {
    assert (!res);
    fprintf (witness_file, "2\n");
    fprintf (witness_file, "b0\n");
  }
  fprintf (witness_file, ".\n");
  fflush (witness_file);
  if (witness_path) fclose (witness_file);
  aiger_reset (model);
  reset_signal_handlers ();
  if (solver) {
    solver->statistics ();
    reset_solver ();
  }
  reset_aigs ();
  msg (0, "");
  stats ();
  msg (0, "");
  if (terminal) msg (0, "\033[1;33mexit %d\033[0m", res);
  else msg (0, "exit %d", res);
  return res;
}
