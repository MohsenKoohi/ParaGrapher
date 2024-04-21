/*
If O_DIRECT access is required for storage accesses, 
ExtendedOpenOption.DIRECT should be used. 
https://stackoverflow.com/a/73604722
The current code does not use ExtendedOpenOption.DIRECT.

*/


import java.io.*;
import java.util.*;
import java.net.*;
import java.nio.file.*;
import java.nio.*;
import java.nio.channels.*;
import java.util.concurrent.atomic.*;
import java.text.SimpleDateFormat ;
import java.util.Date;

public class ReadBandwidth 
{	
	long arraySize = 0;
	String filePath = null;
	int threads_count = Runtime.getRuntime().availableProcessors();	
	long read_blocksize = 4096 * 1024;

	private ReadBandwidth(String[] args)
	{
		String path = null;
		for(int i=0; i< args.length; i++)
		{
			if(args[i].equals("-p") && args.length > i)
				path = args[i + 1];

			if(args[i].equals("-s") && args.length > i)
				arraySize = Long.parseLong(args[i + 1]);

			if(args[i].equals("-t") && args.length > i)
				threads_count = Integer.parseInt(args[i + 1]);

			if(args[i].equals("-b") && args.length > i)
				read_blocksize = Long.parseLong(args[i + 1]);
		}

		assert path != null;
		filePath = path + "/java_read_bandwidth.bin";
		assert arraySize != 0;
		// System.out.println(filePath + "\n" +  arraySize);

		for(int i=0; i< args.length; i++)
		{
			if(args[i].equals("createFile"))
			{
				createFile();
				return;
			}

			if(args[i].equals("mmap"))
			{
				readMMap();
				return;
			}

			if(args[i].equals("read"))
			{
				readRead();
				return;
			}
		}

		return;
	}

	private void readRead()
	{
		try
		{
			File f = new File(filePath);
			assert f.length() >=  8 * arraySize;
		
			long t0 = -System.nanoTime();
			long blocksize = read_blocksize;
			long block_numbers = arraySize * 8 / blocksize;
			Thread tt [] = new Thread[threads_count];
			AtomicInteger counter = new AtomicInteger(0);
			AtomicLong sum = new AtomicLong(0);
			String fp = filePath;
			for(int ti = 0; ti < threads_count; ti++)
			{
				tt[ti] = new Thread()
				{
					public void run()
					{
						try
						{
							long s = 0;
							RandomAccessFile raf = new RandomAccessFile(fp, "r");
							byte buf [] = new byte[(int)blocksize];

							while(true)
							{
								int id = counter.getAndIncrement();
								if( id >= block_numbers)
								{
									raf.close();
									sum.getAndAdd(s);
									return;
								}

								long start = id * blocksize;
								long length = blocksize;
								
								raf.seek(start);
								int ret = raf.read(buf);
								assert ret == blocksize;

								ByteBuffer bl = ByteBuffer.wrap(buf);
								for(int i = 0; i < length / 8; i++)
									s += bl.getLong();
							}
						}
						catch(Exception e)
						{
							System.out.println(e.getMessage());
							e.printStackTrace();
						}
					}
				};
				tt[ti].start();
			}

			for(int ti = 0; ti < threads_count; ti++)
				tt[ti].join();

			t0 += System.nanoTime();
			// System.out.println(sum);
			System.out.println("    " + Math.round(1.0 * arraySize * 8 * 1000 / (double)t0) + ";");
		}
		catch(Exception e)
		{
			System.out.println(e.getMessage());
			e.printStackTrace();
		}

		return;
	}

	private void readMMap()
	{
		try
		{
			File f = new File(filePath);
			assert f.length() >=  8 * arraySize;
		
			long t0 = - System.nanoTime();

			RandomAccessFile raf = new RandomAccessFile(filePath, "r");
			long blocksize = 1024 * 1024;
			long block_numbers = arraySize / blocksize;
			Thread tt [] = new Thread[threads_count];
			AtomicInteger counter = new AtomicInteger(0);
			AtomicLong sum = new AtomicLong(0);
			for(int ti = 0; ti < threads_count; ti++)
			{
				tt[ti] = new Thread()
				{
					public void run()
					{
						long s = 0;

						while(true)
						{
							int id = counter.getAndIncrement();
							if( id >= block_numbers)
							{
								sum.getAndAdd(s);
								return;
							}

							long start = id * blocksize * 8;
							long length = blocksize * 8;
							try
							{
								MappedByteBuffer buf = raf.getChannel().map(FileChannel.MapMode.READ_ONLY, start, length);
								buf.force();
								for(int i = 0; i < length / 8; i++)
									s += buf.getLong();
							}
							catch(Exception e)
							{
								System.out.println(e.getMessage());
								e.printStackTrace();
							}
						}
					}
				};
				tt[ti].start();
			}

			for(int ti = 0; ti < threads_count; ti++)
			{
				tt[ti].join();
			}
			raf.close();

			t0 += System.nanoTime();
			// System.out.println(sum);
			System.out.println("    " + Math.round(1.0 * arraySize * 8 * 1000 / (double)t0) + ";");
		
		}
		catch(Exception e)
		{
			System.out.println(e.getMessage());
			e.printStackTrace();
		}

		return;
	}

	private void createFile()
	{
		try
		{
			File f = new File(filePath);
			if(f.exists() && f.length() != 8 * arraySize)
				f.delete();
			
			if(!f.exists())
			{
				long t0 = - System.nanoTime();

				RandomAccessFile raf = new RandomAccessFile(filePath, "rw");
				raf.setLength(arraySize * 8);

				long blocksize = 1024L * 1024 * 128;
				int block_numbers = (int)((arraySize * 8) / blocksize);
				Thread tt [] = new Thread[threads_count];
				AtomicInteger counter = new AtomicInteger(0);
				for(int t = 0; t < threads_count; t++)
				{
					tt[t] = new Thread()
					{
						public void run()
						{
							while(true)
							{
								int id = counter.getAndIncrement();
								if(id >= block_numbers)
									return;
								long start = id * blocksize;
								long length = blocksize;
								long start_val = start/8;
								try
								{
									MappedByteBuffer buf = raf.getChannel().map(FileChannel.MapMode.READ_WRITE, start, length);
									for(int i = 0; i < length / 8; i++)
										buf.putLong(start_val++);
									buf.force();
								}
								catch(Exception e)
								{
									System.out.println(e.getMessage());
									e.printStackTrace();
								}
							}
						}
					};
					tt[t].start();
				}

				for(int t = 0; t < threads_count; t++)
				{
					tt[t].join();
				}
				raf.close();

				t0 += System.nanoTime();
				System.out.println("Creation time: " + (int)(t0/1e6)/1e3 + "ms");
			}
		}
		catch(Exception e)
		{
			System.out.println(e.getMessage());
			e.printStackTrace();
		}

		return;
	}

	static public void main(String[] args)
	{
		new ReadBandwidth(args);

		return;
	}
}
