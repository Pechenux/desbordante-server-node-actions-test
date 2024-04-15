import { FC } from 'react';
import styles from './Common.module.scss';
import Statistic from '../Statistic';
import prettyBytes from 'pretty-bytes';

interface Props {
  files: number;
  tasks: number;
  freeSpace: number;
}

const Common: FC<Props> = ({ files, tasks, freeSpace }) => {
  return (
    <section className={styles.commonSection}>
      <h5 className={styles.title}>Overview</h5>
      <div className={styles.statistics}>
        <Statistic label="Files uploaded" value={files} />
        <Statistic label="Tasks" value={tasks} />
        <Statistic label="Free space" value={prettyBytes(freeSpace)} />
      </div>
    </section>
  );
};

export default Common;
