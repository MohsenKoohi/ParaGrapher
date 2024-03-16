import java.io.*;
import java.util.*;
import java.net.*;
import java.nio.file.*;
import java.nio.*;
import java.nio.channels.*;
import java.util.concurrent.atomic.*;
import java.text.SimpleDateFormat ;
import java.util.Date;

// Request & Respond Server
public class WebGraphRRServer
{	
	// buffers statuses
	// Changing these values should be reflected also on webgraph.c
	final private long 
		__BS_C_IDLE           = 1, 
		__BS_C_REQUESTED      = 2, 
		__BS_J_READING        = 3, 
		__BS_J_READ_COMPLETED = 4, 
		__BS_C_USER_ACCESS    = 5;

	private MappedByteBuffer buffers_metadata [] = null;
	private AtomicLong finished_buffers = null;
	private String shmFileName = null;
	private WebGraphWriter webgraphWriter = null;

	public WebGraphRRServer(String shmFileName,  WebGraphWriter webgraphWriter)
	{
		this.shmFileName = shmFileName;
		this.webgraphWriter = webgraphWriter;

		return;
	}

	public void start()
	{
		try
		{			
			// Setting the shm buffers
			ByteBuffer longByteBuffer = ByteBuffer.allocate(8).order(ByteOrder.nativeOrder());
			RandomAccessFile shmFile = new RandomAccessFile("/dev/shm/" + shmFileName, "rw");
			MappedByteBuffer Ct_buff = shmFile.getChannel().map(FileChannel.MapMode.READ_ONLY, 0, 8 );     // C_timestamp
			MappedByteBuffer Cc_buff = shmFile.getChannel().map(FileChannel.MapMode.READ_ONLY, 8, 8 );     // C_completed
			MappedByteBuffer Jt_buff = shmFile.getChannel().map(FileChannel.MapMode.READ_WRITE, 64, 8 );   // J_timestamp
			byte temp[] = new byte[8];

			// Reading shared variables
			int buffers_count = 0;
			boolean __PD = false;
			{
				MappedByteBuffer temp_buff = shmFile.getChannel().map(FileChannel.MapMode.READ_ONLY, 16, 24);
				temp_buff.load().get(temp); 
				buffers_count = (int)longByteBuffer.rewind().put(temp).rewind().getLong();
				
				temp_buff.get(temp); // buffer_size
								
				temp_buff.get(temp); 
				__PD = (1 == longByteBuffer.rewind().put(temp).rewind().getLong());
				
			}

			// Setting the buffers
			buffers_metadata = new MappedByteBuffer[buffers_count];
			for(int b = 0; b < buffers_count; b++)
				buffers_metadata[b] = shmFile.getChannel().map(FileChannel.MapMode.READ_WRITE, 64 * (2 + b), 64);
		
			long prev_C_timestamp = 0;
			long J_timestamp = 2;
			long local_finished_buffers = 0;
			finished_buffers = new AtomicLong(0);

			// Looping over requests
			while(true)
			{
				Thread.sleep(200);

				Cc_buff.load().rewind().get(temp); 
				long C_completed = longByteBuffer.rewind().put(temp).rewind().getLong();
				if(C_completed == 1)
					break;
				

				// Check if reading of buffers have been finished
				{
					long t = finished_buffers.get();
					if(t != local_finished_buffers)
					{
						J_timestamp += 2;
						if(J_timestamp < 0)
							J_timestamp = 2;
						Jt_buff.rewind().put(longByteBuffer.rewind().putLong(J_timestamp).array());
						Jt_buff.force();

						local_finished_buffers = t;
					}
				}

				// Check if new requests exist and can be run
				Ct_buff.load().rewind().get(temp); 
				long C_timestamp = longByteBuffer.rewind().put(temp).rewind().getLong();
				if(C_timestamp == prev_C_timestamp)
					continue;
				prev_C_timestamp = C_timestamp;

				for(int b = 0; b < buffers_count; b++)
				{
					buffers_metadata[b].load().rewind().get(temp); 
					long status = longByteBuffer.rewind().put(temp).rewind().getLong();
					if(status != __BS_C_REQUESTED)
						continue;

					buffers_metadata[b].rewind().put(longByteBuffer.rewind().putLong(__BS_J_READING).array());
					buffers_metadata[b].put(longByteBuffer.rewind().putLong(0L).array());
					buffers_metadata[b].force();

					long sv, se, ev, ee;
					buffers_metadata[b].get(temp);
					sv = longByteBuffer.rewind().put(temp).rewind().getLong();
					buffers_metadata[b].get(temp);
					se = longByteBuffer.rewind().put(temp).rewind().getLong();
					buffers_metadata[b].get(temp);
					ev = longByteBuffer.rewind().put(temp).rewind().getLong();
					buffers_metadata[b].get(temp);
					ee = longByteBuffer.rewind().put(temp).rewind().getLong();

					webgraphWriter.writeToBuffer(sv, se, ev, ee, b);
					
					if(__PD)
						System.out.println("[ParaGrapher][JP][RRS] CT: " + C_timestamp + ", JT:" + J_timestamp + " start: " + sv + "." + se + ", end: " + ev + "." + ee);
				}
			}

			if(__PD)
				System.out.println("[ParaGrapher][JP][RRS] Completed");
		}
		catch(Exception e)
		{
			System.out.println(e.getMessage());
			e.printStackTrace();
		}

		return;
	}

	public void bufferWritingCompleted(int buffer_index, long written_edges)
	{
		ByteBuffer lbb = ByteBuffer.allocate(8).order(ByteOrder.nativeOrder());

		buffers_metadata[buffer_index].position(8);
		buffers_metadata[buffer_index].put(lbb.rewind().putLong(written_edges).array());
		buffers_metadata[buffer_index].rewind().put(lbb.rewind().putLong(__BS_J_READ_COMPLETED).array());
		buffers_metadata[buffer_index].force();

		finished_buffers.incrementAndGet();

		return;
	}
}