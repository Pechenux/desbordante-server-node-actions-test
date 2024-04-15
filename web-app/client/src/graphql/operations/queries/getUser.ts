import { gql } from '@apollo/client';

export const GET_USER = gql`
  query getUser {
    user {
      userID
      fullName
      email
      permissions
      accountStatus
      reservedDiskSpace
      remainingDiskSpace
      tasks(withDeleted: false) {
        total
        data {
          taskID
          state {
            ... on TaskState {
              user {
                fullName
              }
              processStatus
              phaseName
              currentPhase
              progress
              maxPhase
              isExecuted
              elapsedTime
              createdAt
            }
          }
          data {
            baseConfig {
              algorithmName
              type
            }
          }
          dataset {
            originalFileName
          }
        }
      }
      datasets(sort: { sortBy: FILE_SIZE, orderBy: DESC }) {
        total
        data {
          fileID
          fileName
          hasHeader
          delimiter
          supportedPrimitives
          rowsCount
          fileSize
          fileFormat {
            inputFormat
            tidColumnIndex
            itemColumnIndex
            hasTid
          }
          countOfColumns
          isBuiltIn
          createdAt
          originalFileName
          numberOfUses
        }
      }
    }
  }
`;
