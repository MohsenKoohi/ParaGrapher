public interface WebGraphWriter
{
	public void writeToBuffer(long startVertex, long startEdge, long endVertex, long endEdge, int bufferID);
}
