#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "parser.h"
#include "status.h"
#include "column.h"

/* Validate AST columns and value types against the fixed schema. */
SemanticStatus semantic_validate(const AST *ast);

#endif
