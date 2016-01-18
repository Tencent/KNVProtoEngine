 KNV(Key-N-Value) is a protocol engine for manipulating protocol data without knowing the detail meaning of the content.
 KNV serves for 3 main purposes:
  1) As a fast protocol engine. KNV can manipulate part of PB(Google's Protocol Buffers) data without decoding all the contents, and is highly optimized for raw handling of data.
  2) As a general proxy server.
  3) As a general data storage server.

 Key-N-Value philosophy:
  A KNV tree is common PB tree, except that each node is identified by Tag + Key.
  Tag is the PB tag number, Key is the value of a special sub node with tag=1.
  A Leaf is a special node that is either non-expandable or has not been expanded.

  The first 10 tag numbers are reserved for META data in a node,
  if you wish KNV tree to manage nodes, please define your data nodes beginning at tag 11.



Modification history
 2013-10-12	  Yu Zhenshen       Created
 2013-11-04   Yu Zhenshen       Add parent pointer and eval_size to optimize folding
 2014-01-17   Yu Zhenshen       Use mem_pool for dynamic memory management
 2014-01-28   Yu Zhenshen       Use KnvHt to optimize hash initialization
 2014-05-17   Yu Zhenshen       Meta use KnvNode instead of KnvLeaf


Example usage for PB encoding/decoding:

Step 1, write your own .proto file:

--------test.proto-----------------
message TestRequestBody
{
	optional string user_id = 1;
	optional uint32 operation = 11;
	optional string reason = 12;
};

message TestResponseBody
{
	optional string result = 11;
};
---------end of test.proto-----------

Step 2, use knvshow to generate a corresponding .h file:

Execute:
	./knvshow -p test.proto > test.knv.h

--------test.knv.h-------------------
enum TestRequestBodyTags
{
	TestRequestBodyTag_user_id = 1,
	TestRequestBodyTag_operation = 11,
	TestRequestBodyTag_reason = 12,
};
 
enum TestResponseBodyTags
{
	TestResponseBodyTag_result = 11,
};
-------end of test.knv.h-------------


Step 3, write your own code that handles protocol with knv API:

KnvNode *req = KnvNode::NewTree(1); // Eache node requires a tag for KNV, but each PB message needs not if it is a root node, so here we assign one anyway
if(req==NULL) // something goes wrong
{
	cout << "KnvNode::NewTree failed: " << KnvNode::GetGlobalErrorMsg() << endl;
	return -1;
}
req->SetFieldStr(TestRequestBodyTag_user_id, 4, "test");
req->SetFieldInt(TestRequestBodyTag_operation, 1);
req->SetFieldStr(TestRequestBodyTag_reason, 8, "for test");
char buffer[1024];
int len = sizeof(buffer);
req->Serialize(buffer, &len, false); // not encoding header tag

KnvNode::Delete(req); // you should delete req when it is no longer in use

// Now buffer contains the same data as TestRequestBody.SerialzeToString()
// Next, write your own code that handles remote procedure call, we assume that buffer contains the whole response packet

string rsp_body(buffer, len);
KnvNode *rsp = KnvNode::NewFromMessage(rsp_body);
cout << rsp->GetFieldStr(TestResponseBodyTag_result) << endl;
KnvNode::Delete(rsp); // you should delete rsp when it is no longer in use

