import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import styles from '../styles/Home.module.css'
import { GitHub } from '@mui/icons-material'

const Home: NextPage = () => {
  return (
    <div className={styles.container}>
      <Head>
        <title>TS-Live!</title>
        <meta
          name='description'
          content='TS-Live! experimental mirakurun client for browser'
        />
        <link rel='icon' href='/favicon.ico' />
      </Head>

      <main className={styles.main}>
        <h1 className={styles.title}>
          <a href='https://ts-live.pages.dev/'>TS-Live!</a>
        </h1>

        <div className={styles.grid}>
          <a href='/live' className={styles.card}>
            <h2>Live &rarr;</h2>
            <p>ライブ視聴</p>
          </a>

          <a href='/' className={styles.card}>
            <h2>File &rarr;</h2>
            <p>録画ファイル再生 (Comming Soon...)</p>
          </a>

          <a
            href='https://github.com/vercel/next.js/tree/canary/examples'
            className={styles.card}
          >
            <h2>Examples &rarr;</h2>
            <p>Discover and deploy boilerplate example Next.js projects.</p>
          </a>

          <a
            href='https://vercel.com/new?utm_source=create-next-app&utm_medium=default-template&utm_campaign=create-next-app'
            className={styles.card}
          >
            <h2>Deploy &rarr;</h2>
            <p>
              Instantly deploy your Next.js site to a public URL with Vercel.
            </p>
          </a>
        </div>
      </main>

      <footer className={styles.footer}>
        <a
          href='https://vercel.com?utm_source=create-next-app&utm_medium=default-template&utm_campaign=create-next-app'
          target='_blank'
          rel='noopener noreferrer'
        >
          Powered by{' '}
          <span className={styles.logo}>
            <Image src='/vercel.svg' alt='Vercel Logo' width={72} height={16} />
          </span>
        </a>
      </footer>
    </div>
  )
}

export default Home
