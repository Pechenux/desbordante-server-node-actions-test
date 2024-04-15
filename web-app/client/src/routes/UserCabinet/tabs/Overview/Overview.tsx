import { FC } from 'react';
import Common from './components/Common';
import Tasks from './components/Tasks';
import Files from './components/Files';
import styles from './Overview.module.scss';
import { useQuery } from '@apollo/client';
import { GET_USER } from '@graphql/operations/queries/getUser';
import { getUser } from '@graphql/operations/queries/__generated__/getUser';

const SystemStatus: FC = () => {
  const { data } = useQuery<getUser>(GET_USER, {
    variables: {
      userID: undefined,
    },
  });

  const user = data?.user;

  if (!user) {
    return null;
  }

  const { datasets, tasks, remainingDiskSpace, reservedDiskSpace } = user;

  return (
    <div className={styles.overviewTab}>
      <div className={styles.left}>
        <Common
          files={datasets.total}
          tasks={tasks.total}
          freeSpace={remainingDiskSpace}
        />
        <Tasks tasks={tasks.data} />
      </div>
      <Files
        files={datasets.data}
        reservedSpace={reservedDiskSpace}
        usedSpace={reservedDiskSpace - remainingDiskSpace}
      />
    </div>
  );
};

export default SystemStatus;
