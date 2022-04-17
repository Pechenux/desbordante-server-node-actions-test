import {ARSortBy, OrderBy, Pagination, SortBy, SortSide,} from "../types/globalTypes";
import {PrimitiveFilter} from "../types/primitives";

export const primitiveTypeList = [
  "Functional Dependencies",
  "Conditional Functional Dependencies",
  "Association Rules",
  "Error Detection Pipeline",
] as const;

export const sortOptions = {
  FD: Object.keys(SortSide) as SortSide[],
  CFD: Object.keys(SortSide) as SortSide[],
  AR: Object.keys(SortSide) as SortSide[],
};

export const defaultPrimitiveFilter: PrimitiveFilter = {
  FD: {
    filterString: null,
    mustContainLhsColIndices: null,
    mustContainRhsColIndices: null,
    orderBy: OrderBy.ASC,
    sortBy: SortBy.COL_NAME,
    sortSide: SortSide.LHS,
    pagination: {
      limit: 10,
      offset: 0,
    },
    withoutKeys: false,
  },
  CFD: {
    limit: 10,
    offset: 0,
  },
  AR: {
    filterString: null,
    orderBy: OrderBy.DESC,
    sortBy: ARSortBy.DEFAULT,
    pagination: {
      limit: 10,
      offset: 0,
    }
  },
  TypoFD: {
    limit: 10,
    offset: 0,
  }
};

export const defaultDatasetPagination: Pagination = {
  limit: 100,
  offset: 0,
};