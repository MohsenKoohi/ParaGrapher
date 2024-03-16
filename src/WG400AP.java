import java.io.*;
import java.util.*;
import java.net.*;
import java.nio.file.*;
import java.nio.*;
import java.nio.channels.*;
import it.unimi.dsi.webgraph.*;
import it.unimi.dsi.webgraph.labelling.*;
import java.util.concurrent.atomic.*;
import java.text.SimpleDateFormat ;
import java.util.Date;

public class WG400AP implements WebGraphWriter
{
	private int threadsCount = Runtime.getRuntime().availableProcessors();
	private long degrees [] = null;
	
	private MappedByteBuffer buffers [] = null;
	private ImmutableGraph graph = null;
	private WebGraphRRServer rrs = null;
	
	private WG400AP(String[] args)
	{
		assert(args.length >=3);
		
		if(args[0].trim().equals("create_bin_offsets"))
			createBinOffsets(args[1], args[2]);

		if(args[0].trim().equals("read_edges"))
			readEdges(args[1], args[2]);

		return;
	}

	private void readEdges(String graphPath, String shmFileName)
	{
		try
		{			

			// Setting the shm buffers
			ByteBuffer longByteBuffer = ByteBuffer.allocate(8).order(ByteOrder.nativeOrder());
			RandomAccessFile shmFile = new RandomAccessFile("/dev/shm/" + shmFileName, "rw");
			byte temp[] = new byte[8];

			// Reading shared variables
			int buffers_count = 0;
			long buffer_size = 0;
			boolean __PD = false;
			long bytes_per_edge = 0;
			{
				MappedByteBuffer temp_buff = shmFile.getChannel().map(FileChannel.MapMode.READ_ONLY, 16, 32);
				temp_buff.load().get(temp); 
				buffers_count = (int)longByteBuffer.rewind().put(temp).rewind().getLong();
				
				temp_buff.get(temp); 
				buffer_size = longByteBuffer.rewind().put(temp).rewind().getLong();
				
				temp_buff.get(temp); 
				__PD = (1 == longByteBuffer.rewind().put(temp).rewind().getLong());
				
				temp_buff.get(temp); 
				bytes_per_edge = longByteBuffer.rewind().put(temp).rewind().getLong();
				
				if(__PD)
				{
					System.out.print("[ParaGrapher][JP] graph:" + graphPath + ", shmFileName: " + shmFileName + ", tc: " + threadsCount+"\n");
				}

				// Loading the graph 
				long loadtime = -System.nanoTime();
				graph = ImmutableGraph.loadMapped(graphPath);
				loadtime += System.nanoTime();
				

				if(!graph.randomAccess())
				{
					System.out.println("[ParaGrapher][JP] graph is not a random access graph.");
					return;
				}

				if(__PD)
				{
					System.out.print("[ParaGrapher][JP] Graph metadata loaded in : " + String.format("%,d",(long)(loadtime/1e6)) + " ms\n");
					System.out.print("[ParaGrapher][JP] |V|: " + String.format("%,d",graph.numNodes()) + ", |E|: " + String.format("%,d",graph.numArcs()));
					System.out.print(", buffers_count: " + String.format("%,d",buffers_count) + ", buffer_size: " + String.format("%,d",buffer_size) + ", bytes_per_edge: " + bytes_per_edge);
					System.out.println();
				}
			}

			// Setting the buffers
			buffers = new MappedByteBuffer[buffers_count];
			for(int b = 0; b < buffers_count; b++)
			{
				buffers[b] = shmFile.getChannel().map(FileChannel.MapMode.READ_WRITE, 
					64 * 2 + 64 * buffers_count + b * buffer_size * bytes_per_edge, 
					buffer_size * bytes_per_edge
				);
			}

			rrs = new WebGraphRRServer(shmFileName, this);
			rrs.start();

		}
		catch(Exception e)
		{
			System.out.println(e.getMessage());
			e.printStackTrace();
		}

		return;
	}


	public void writeToBuffer(long startVertex, long startEdge, long endVertex, long endEdge, int bufferID)
	{
		new ReaderThread(startVertex, startEdge, endVertex, endEdge, bufferID).start();
		
		return;
	}

	private class ReaderThread extends Thread
	{
		private final long sv, se, ev, ee;
		private final int buffer_index;
		private ImmutableGraph g;

		private ReaderThread(long sv, long se, long ev, long ee, int bi)
		{
			this.sv = sv;
			this.se = se;
			this.ev = ev;
			this.ee = ee;
			this.buffer_index = bi;
			this.g = graph.copy();

			return;
		}

		public void run()
		{
			try
			{
				ByteBuffer lbb = ByteBuffer.allocate(8).order(ByteOrder.nativeOrder());
				ByteBuffer ibb = ByteBuffer.allocate(4).order(ByteOrder.nativeOrder());
				long written_edges = 0;

				buffers[buffer_index].load().rewind();

				for(long v = sv; v <= ev; v++)
				{
					if(v >= g.numNodes())
						break;

					long degree = g.outdegree((int)v);
					LazyIntIterator it = g.successors((int)v);

					int n = 0;
					if( v == sv)
						while(n < se)
						{
							int dest = it.nextInt();
							assert dest != -1;
							
							n++;
						}

					int le_index = (int)degree;
					if(v == ev)
						le_index = (int)Math.min(degree, ee);

					while(n < le_index)
					{
						int dest = it.nextInt();
						assert dest != -1;

						buffers[buffer_index].put(ibb.rewind().putInt(dest).array());
						
						written_edges++;
						n++;
					}
				}

				buffers[buffer_index].force();

				// Set the metadata of this buffer and inform the RRS to increment its timestamp
				rrs.bufferWritingCompleted(buffer_index, written_edges);
			}
			catch(Exception e)
			{
				System.out.println(e.getMessage());
				e.printStackTrace();
			}

			return;
		}
	}

	private void createBinOffsets(String graphPath, String offsetsFilePath)
	{
		System.out.println("WG400AP|createBinOffsets()\ngraph:" + graphPath + "\nout:" + offsetsFilePath + ", tc:" + threadsCount);

		try
		{			
			// Loading the graph 
				ImmutableGraph graph = ImmutableGraph.loadMapped(graphPath);
				System.out.print("|V|: " + String.format("%,d",graph.numNodes()));
				System.out.println(", |E|: " + String.format("%,d",graph.numArcs()));

			// Reading degrees
				Thread threads [] = new Thread[threadsCount];
				degrees = new long[graph.numNodes() + 1];

				for(int t=0; t < threadsCount; t++)
				{
					threads[t] = new OffsetsThread(t, graph);
					threads[t].start();
				}

				for(int t=0; t<threadsCount; t++)
					threads[t].join();			

			// Writing offsets to the file
				ByteBuffer longByteBuffer = ByteBuffer.allocate(8).order(ByteOrder.nativeOrder());
				RandomAccessFile outFile = new RandomAccessFile(offsetsFilePath, "rw");

				long b_ts = 8L * (1 + graph.numNodes()); // buffer total size
				long b_count = 0; // counter for written bytes
				long b_cof = 0; // current offset
				long b_length = Math.min(1L<<30, b_ts - b_cof);
				MappedByteBuffer buffer = outFile.getChannel().map(FileChannel.MapMode.READ_WRITE, b_cof, b_length);
				assert buffer.limit() == b_length;

				long sum = 0;
				buffer.put(longByteBuffer.rewind().putLong(sum).array());
				b_count += 8;

				for(int v = 0; v < graph.numNodes(); v++)
				{
					sum += degrees[v];
					buffer.put(longByteBuffer.rewind().putLong(sum).array());
				
					b_count += 8;
					if(b_count == b_length)
					{
						buffer.force();
						b_cof += b_length;
						b_length = Math.min(1L<<30, b_ts - b_cof);
						buffer = outFile.getChannel().map(FileChannel.MapMode.READ_WRITE, b_cof, b_length);
						assert buffer.limit() == b_length;
						b_count = 0;		
					}
				}

				assert b_count == b_length;
				assert sum == graph.numArcs();
				buffer.force();
				outFile.close();
		}
		catch(Exception e)
		{
			System.out.println(e.getMessage());
			e.printStackTrace();
		}

		return;
	}

	private class OffsetsThread extends Thread
	{
		private int partition;
		private int totalPartitions;
		private ImmutableGraph graph;

		private OffsetsThread(int partition, ImmutableGraph graph)
		{
			this.partition = partition;
			this.totalPartitions = threadsCount;
			this.graph = graph.copy();
		}

		public void run()
		{
			int startVertex = partition * (graph.numNodes() / totalPartitions);
			int endVertex = (partition + 1) * (graph.numNodes() / totalPartitions);
			if(partition == totalPartitions -1)
				endVertex = graph.numNodes();

			for(int v = startVertex; v < endVertex; v++)
			{
				// assert it.nextInt() == v;
				degrees[v] = graph.outdegree(v); 
			}

			return;
		}
	}

	static public void main(String[] args)
	{
		new WG400AP(args);

		return;
	}
}