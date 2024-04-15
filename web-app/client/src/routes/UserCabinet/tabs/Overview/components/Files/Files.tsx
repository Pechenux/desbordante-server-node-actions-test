import { FC } from 'react';
import prettyBytes from 'pretty-bytes';
import styles from './Files.module.scss';
import { getUser_user_datasets_data } from '@graphql/operations/queries/__generated__/getUser';
import FileItem from '../FileItem';
import { Pie, PieChart, ResponsiveContainer } from 'recharts';

interface Props {
  files: getUser_user_datasets_data[] | null;
  reservedSpace: number;
  usedSpace: number;
}

const Files: FC<Props> = ({ files, reservedSpace, usedSpace }) => {
  const chartData = [
    {
      value: usedSpace,
      name: 'Used',
    },
    {
      value: reservedSpace - usedSpace,
      name: 'Remaining',
    },
  ];

  return (
    <section className={styles.filesSection}>
      <ResponsiveContainer width="100%" height="100%">
        <PieChart width={400} height={400}>
          <Pie
            dataKey="value"
            startAngle={180}
            endAngle={0}
            data={chartData}
            cx="50%"
            cy="50%"
            outerRadius={80}
            fill={}
            label
          />
        </PieChart>
      </ResponsiveContainer>
      <div className={styles.stats}>
        <h5>{prettyBytes(usedSpace)}</h5>
        <p>used of {prettyBytes(reservedSpace)}</p>
      </div>
      <ul className={styles.files}>
        {files?.map((file) => (
          <FileItem
            data={file}
            percentage={file.fileSize / reservedSpace}
            key={file.fileID}
          />
        ))}
      </ul>
    </section>
  );
};

export default Files;
