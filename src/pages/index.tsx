import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import styles from '../styles/Home.module.css'
import { GitHub } from '@mui/icons-material'
import Script from 'next/script'
import Link from 'next/link'

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
      <Script
        src='https://www.googletagmanager.com/gtag/js?id=G-SR7L1XYNV0'
        strategy='afterInteractive'
      />
      <Script id='google-analytics' strategy='afterInteractive'>
        {`
            window.dataLayer = window.dataLayer || [];
            function gtag(){window.dataLayer.push(arguments);}
            gtag('js', new Date());

            gtag('config', 'G-SR7L1XYNV0');
          `}
      </Script>

      <main className={styles.main}>
        <h1 className={styles.title}>
          <a href='https://ts-live.pages.dev/'>TS-Live!</a>
        </h1>

        <div className={styles.grid}>
          <Link href='/live'>
            <a className={styles.card}>
              <h2>Live &rarr;</h2>
              <p>ライブ視聴</p>
            </a>
          </Link>

          <Link href='/file'>
            <a className={styles.card}>
              <h2>File &rarr;</h2>
              <p>ファイル再生</p>
            </a>
          </Link>
        </div>
      </main>

      <footer className={styles.footer}></footer>
    </div>
  )
}

export default Home
