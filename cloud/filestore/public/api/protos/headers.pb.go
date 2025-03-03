// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.19.0
// source: cloud/filestore/public/api/protos/headers.proto

package protos

import (
	protos "github.com/ydb-platform/nbs/cloud/storage/core/protos"
	protos1 "github.com/ydb-platform/nbs/library/cpp/lwtrace/protos"
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type THeaders struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Trace identifier for logging.
	TraceId string `protobuf:"bytes,1,opt,name=TraceId,proto3" json:"TraceId,omitempty"`
	// Idempotence identifier for retries.
	IdempotenceId string `protobuf:"bytes,2,opt,name=IdempotenceId,proto3" json:"IdempotenceId,omitempty"`
	// Client identifier for client detection.
	ClientId string `protobuf:"bytes,3,opt,name=ClientId,proto3" json:"ClientId,omitempty"`
	// Session identifier.
	SessionId string `protobuf:"bytes,4,opt,name=SessionId,proto3" json:"SessionId,omitempty"`
	// Request timestamp.
	Timestamp uint64 `protobuf:"varint,5,opt,name=Timestamp,proto3" json:"Timestamp,omitempty"`
	// Request identifier.
	RequestId uint64 `protobuf:"varint,6,opt,name=RequestId,proto3" json:"RequestId,omitempty"`
	// Request timeout (in milliseconds).
	RequestTimeout uint32 `protobuf:"varint,7,opt,name=RequestTimeout,proto3" json:"RequestTimeout,omitempty"`
	// Session sequence endpoint set by endpoint.
	SessionSeqNo uint64 `protobuf:"varint,8,opt,name=SessionSeqNo,proto3" json:"SessionSeqNo,omitempty"`
	// Internal header, should not be used outside.
	Internal   *THeaders_TInternal `protobuf:"bytes,9,opt,name=Internal,proto3" json:"Internal,omitempty"`
	OriginFqdn string              `protobuf:"bytes,10,opt,name=OriginFqdn,proto3" json:"OriginFqdn,omitempty"`
}

func (x *THeaders) Reset() {
	*x = THeaders{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_filestore_public_api_protos_headers_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *THeaders) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*THeaders) ProtoMessage() {}

func (x *THeaders) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_filestore_public_api_protos_headers_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use THeaders.ProtoReflect.Descriptor instead.
func (*THeaders) Descriptor() ([]byte, []int) {
	return file_cloud_filestore_public_api_protos_headers_proto_rawDescGZIP(), []int{0}
}

func (x *THeaders) GetTraceId() string {
	if x != nil {
		return x.TraceId
	}
	return ""
}

func (x *THeaders) GetIdempotenceId() string {
	if x != nil {
		return x.IdempotenceId
	}
	return ""
}

func (x *THeaders) GetClientId() string {
	if x != nil {
		return x.ClientId
	}
	return ""
}

func (x *THeaders) GetSessionId() string {
	if x != nil {
		return x.SessionId
	}
	return ""
}

func (x *THeaders) GetTimestamp() uint64 {
	if x != nil {
		return x.Timestamp
	}
	return 0
}

func (x *THeaders) GetRequestId() uint64 {
	if x != nil {
		return x.RequestId
	}
	return 0
}

func (x *THeaders) GetRequestTimeout() uint32 {
	if x != nil {
		return x.RequestTimeout
	}
	return 0
}

func (x *THeaders) GetSessionSeqNo() uint64 {
	if x != nil {
		return x.SessionSeqNo
	}
	return 0
}

func (x *THeaders) GetInternal() *THeaders_TInternal {
	if x != nil {
		return x.Internal
	}
	return nil
}

func (x *THeaders) GetOriginFqdn() string {
	if x != nil {
		return x.OriginFqdn
	}
	return ""
}

type TResponseHeaders struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Response traces.
	Trace *protos.TTraceInfo `protobuf:"bytes,1,opt,name=Trace,proto3" json:"Trace,omitempty"`
	// Response throttler info.
	Throttler *protos.TThrottlerInfo `protobuf:"bytes,2,opt,name=Throttler,proto3" json:"Throttler,omitempty"`
}

func (x *TResponseHeaders) Reset() {
	*x = TResponseHeaders{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_filestore_public_api_protos_headers_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TResponseHeaders) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TResponseHeaders) ProtoMessage() {}

func (x *TResponseHeaders) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_filestore_public_api_protos_headers_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TResponseHeaders.ProtoReflect.Descriptor instead.
func (*TResponseHeaders) Descriptor() ([]byte, []int) {
	return file_cloud_filestore_public_api_protos_headers_proto_rawDescGZIP(), []int{1}
}

func (x *TResponseHeaders) GetTrace() *protos.TTraceInfo {
	if x != nil {
		return x.Trace
	}
	return nil
}

func (x *TResponseHeaders) GetThrottler() *protos.TThrottlerInfo {
	if x != nil {
		return x.Throttler
	}
	return nil
}

type THeaders_TInternal struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Trace         *protos1.TTraceRequest `protobuf:"bytes,1,opt,name=Trace,proto3" json:"Trace,omitempty"`
	RequestSource protos.ERequestSource  `protobuf:"varint,2,opt,name=RequestSource,proto3,enum=NCloud.NProto.ERequestSource" json:"RequestSource,omitempty"`
	// IAM auth token.
	AuthToken string `protobuf:"bytes,3,opt,name=AuthToken,proto3" json:"AuthToken,omitempty"`
}

func (x *THeaders_TInternal) Reset() {
	*x = THeaders_TInternal{}
	if protoimpl.UnsafeEnabled {
		mi := &file_cloud_filestore_public_api_protos_headers_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *THeaders_TInternal) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*THeaders_TInternal) ProtoMessage() {}

func (x *THeaders_TInternal) ProtoReflect() protoreflect.Message {
	mi := &file_cloud_filestore_public_api_protos_headers_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use THeaders_TInternal.ProtoReflect.Descriptor instead.
func (*THeaders_TInternal) Descriptor() ([]byte, []int) {
	return file_cloud_filestore_public_api_protos_headers_proto_rawDescGZIP(), []int{0, 0}
}

func (x *THeaders_TInternal) GetTrace() *protos1.TTraceRequest {
	if x != nil {
		return x.Trace
	}
	return nil
}

func (x *THeaders_TInternal) GetRequestSource() protos.ERequestSource {
	if x != nil {
		return x.RequestSource
	}
	return protos.ERequestSource_SOURCE_INSECURE_CONTROL_CHANNEL
}

func (x *THeaders_TInternal) GetAuthToken() string {
	if x != nil {
		return x.AuthToken
	}
	return ""
}

var File_cloud_filestore_public_api_protos_headers_proto protoreflect.FileDescriptor

var file_cloud_filestore_public_api_protos_headers_proto_rawDesc = []byte{
	0x0a, 0x2f, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x66, 0x69, 0x6c, 0x65, 0x73, 0x74, 0x6f, 0x72,
	0x65, 0x2f, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x2f, 0x61, 0x70, 0x69, 0x2f, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x73, 0x2f, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x2e, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x12, 0x18, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x46, 0x69, 0x6c, 0x65, 0x53,
	0x74, 0x6f, 0x72, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x2e, 0x63, 0x6c, 0x6f,
	0x75, 0x64, 0x2f, 0x73, 0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f,
	0x70, 0x72, 0x6f, 0x74, 0x6f, 0x73, 0x2f, 0x72, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x5f, 0x73,
	0x6f, 0x75, 0x72, 0x63, 0x65, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x29, 0x63, 0x6c, 0x6f,
	0x75, 0x64, 0x2f, 0x73, 0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f,
	0x70, 0x72, 0x6f, 0x74, 0x6f, 0x73, 0x2f, 0x74, 0x68, 0x72, 0x6f, 0x74, 0x74, 0x6c, 0x65, 0x72,
	0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x25, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x73, 0x74,
	0x6f, 0x72, 0x61, 0x67, 0x65, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x73, 0x2f, 0x74, 0x72, 0x61, 0x63, 0x65, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x28, 0x6c,
	0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x2f, 0x63, 0x70, 0x70, 0x2f, 0x6c, 0x77, 0x74, 0x72, 0x61,
	0x63, 0x65, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x73, 0x2f, 0x6c, 0x77, 0x74, 0x72, 0x61, 0x63,
	0x65, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0x96, 0x04, 0x0a, 0x08, 0x54, 0x48, 0x65, 0x61,
	0x64, 0x65, 0x72, 0x73, 0x12, 0x18, 0x0a, 0x07, 0x54, 0x72, 0x61, 0x63, 0x65, 0x49, 0x64, 0x18,
	0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x07, 0x54, 0x72, 0x61, 0x63, 0x65, 0x49, 0x64, 0x12, 0x24,
	0x0a, 0x0d, 0x49, 0x64, 0x65, 0x6d, 0x70, 0x6f, 0x74, 0x65, 0x6e, 0x63, 0x65, 0x49, 0x64, 0x18,
	0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0d, 0x49, 0x64, 0x65, 0x6d, 0x70, 0x6f, 0x74, 0x65, 0x6e,
	0x63, 0x65, 0x49, 0x64, 0x12, 0x1a, 0x0a, 0x08, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x49, 0x64,
	0x18, 0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x08, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x49, 0x64,
	0x12, 0x1c, 0x0a, 0x09, 0x53, 0x65, 0x73, 0x73, 0x69, 0x6f, 0x6e, 0x49, 0x64, 0x18, 0x04, 0x20,
	0x01, 0x28, 0x09, 0x52, 0x09, 0x53, 0x65, 0x73, 0x73, 0x69, 0x6f, 0x6e, 0x49, 0x64, 0x12, 0x1c,
	0x0a, 0x09, 0x54, 0x69, 0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d, 0x70, 0x18, 0x05, 0x20, 0x01, 0x28,
	0x04, 0x52, 0x09, 0x54, 0x69, 0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d, 0x70, 0x12, 0x1c, 0x0a, 0x09,
	0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x49, 0x64, 0x18, 0x06, 0x20, 0x01, 0x28, 0x04, 0x52,
	0x09, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x49, 0x64, 0x12, 0x26, 0x0a, 0x0e, 0x52, 0x65,
	0x71, 0x75, 0x65, 0x73, 0x74, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x18, 0x07, 0x20, 0x01,
	0x28, 0x0d, 0x52, 0x0e, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x54, 0x69, 0x6d, 0x65, 0x6f,
	0x75, 0x74, 0x12, 0x22, 0x0a, 0x0c, 0x53, 0x65, 0x73, 0x73, 0x69, 0x6f, 0x6e, 0x53, 0x65, 0x71,
	0x4e, 0x6f, 0x18, 0x08, 0x20, 0x01, 0x28, 0x04, 0x52, 0x0c, 0x53, 0x65, 0x73, 0x73, 0x69, 0x6f,
	0x6e, 0x53, 0x65, 0x71, 0x4e, 0x6f, 0x12, 0x48, 0x0a, 0x08, 0x49, 0x6e, 0x74, 0x65, 0x72, 0x6e,
	0x61, 0x6c, 0x18, 0x09, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x2c, 0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75,
	0x64, 0x2e, 0x4e, 0x46, 0x69, 0x6c, 0x65, 0x53, 0x74, 0x6f, 0x72, 0x65, 0x2e, 0x4e, 0x50, 0x72,
	0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x73, 0x2e, 0x54, 0x49, 0x6e,
	0x74, 0x65, 0x72, 0x6e, 0x61, 0x6c, 0x52, 0x08, 0x49, 0x6e, 0x74, 0x65, 0x72, 0x6e, 0x61, 0x6c,
	0x12, 0x1e, 0x0a, 0x0a, 0x4f, 0x72, 0x69, 0x67, 0x69, 0x6e, 0x46, 0x71, 0x64, 0x6e, 0x18, 0x0a,
	0x20, 0x01, 0x28, 0x09, 0x52, 0x0a, 0x4f, 0x72, 0x69, 0x67, 0x69, 0x6e, 0x46, 0x71, 0x64, 0x6e,
	0x1a, 0x9d, 0x01, 0x0a, 0x09, 0x54, 0x49, 0x6e, 0x74, 0x65, 0x72, 0x6e, 0x61, 0x6c, 0x12, 0x2d,
	0x0a, 0x05, 0x54, 0x72, 0x61, 0x63, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x17, 0x2e,
	0x4e, 0x4c, 0x57, 0x54, 0x72, 0x61, 0x63, 0x65, 0x2e, 0x54, 0x54, 0x72, 0x61, 0x63, 0x65, 0x52,
	0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x52, 0x05, 0x54, 0x72, 0x61, 0x63, 0x65, 0x12, 0x43, 0x0a,
	0x0d, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x53, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x18, 0x02,
	0x20, 0x01, 0x28, 0x0e, 0x32, 0x1d, 0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x50,
	0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x45, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x53, 0x6f, 0x75,
	0x72, 0x63, 0x65, 0x52, 0x0d, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x53, 0x6f, 0x75, 0x72,
	0x63, 0x65, 0x12, 0x1c, 0x0a, 0x09, 0x41, 0x75, 0x74, 0x68, 0x54, 0x6f, 0x6b, 0x65, 0x6e, 0x18,
	0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x09, 0x41, 0x75, 0x74, 0x68, 0x54, 0x6f, 0x6b, 0x65, 0x6e,
	0x22, 0x80, 0x01, 0x0a, 0x10, 0x54, 0x52, 0x65, 0x73, 0x70, 0x6f, 0x6e, 0x73, 0x65, 0x48, 0x65,
	0x61, 0x64, 0x65, 0x72, 0x73, 0x12, 0x2f, 0x0a, 0x05, 0x54, 0x72, 0x61, 0x63, 0x65, 0x18, 0x01,
	0x20, 0x01, 0x28, 0x0b, 0x32, 0x19, 0x2e, 0x4e, 0x43, 0x6c, 0x6f, 0x75, 0x64, 0x2e, 0x4e, 0x50,
	0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x54, 0x72, 0x61, 0x63, 0x65, 0x49, 0x6e, 0x66, 0x6f, 0x52,
	0x05, 0x54, 0x72, 0x61, 0x63, 0x65, 0x12, 0x3b, 0x0a, 0x09, 0x54, 0x68, 0x72, 0x6f, 0x74, 0x74,
	0x6c, 0x65, 0x72, 0x18, 0x02, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x1d, 0x2e, 0x4e, 0x43, 0x6c, 0x6f,
	0x75, 0x64, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x54, 0x68, 0x72, 0x6f, 0x74,
	0x74, 0x6c, 0x65, 0x72, 0x49, 0x6e, 0x66, 0x6f, 0x52, 0x09, 0x54, 0x68, 0x72, 0x6f, 0x74, 0x74,
	0x6c, 0x65, 0x72, 0x42, 0x34, 0x5a, 0x32, 0x61, 0x2e, 0x79, 0x61, 0x6e, 0x64, 0x65, 0x78, 0x2d,
	0x74, 0x65, 0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x63, 0x6c, 0x6f, 0x75, 0x64, 0x2f, 0x66, 0x69,
	0x6c, 0x65, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x2f, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x2f, 0x61,
	0x70, 0x69, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x73, 0x62, 0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x33,
}

var (
	file_cloud_filestore_public_api_protos_headers_proto_rawDescOnce sync.Once
	file_cloud_filestore_public_api_protos_headers_proto_rawDescData = file_cloud_filestore_public_api_protos_headers_proto_rawDesc
)

func file_cloud_filestore_public_api_protos_headers_proto_rawDescGZIP() []byte {
	file_cloud_filestore_public_api_protos_headers_proto_rawDescOnce.Do(func() {
		file_cloud_filestore_public_api_protos_headers_proto_rawDescData = protoimpl.X.CompressGZIP(file_cloud_filestore_public_api_protos_headers_proto_rawDescData)
	})
	return file_cloud_filestore_public_api_protos_headers_proto_rawDescData
}

var file_cloud_filestore_public_api_protos_headers_proto_msgTypes = make([]protoimpl.MessageInfo, 3)
var file_cloud_filestore_public_api_protos_headers_proto_goTypes = []interface{}{
	(*THeaders)(nil),              // 0: NCloud.NFileStore.NProto.THeaders
	(*TResponseHeaders)(nil),      // 1: NCloud.NFileStore.NProto.TResponseHeaders
	(*THeaders_TInternal)(nil),    // 2: NCloud.NFileStore.NProto.THeaders.TInternal
	(*protos.TTraceInfo)(nil),     // 3: NCloud.NProto.TTraceInfo
	(*protos.TThrottlerInfo)(nil), // 4: NCloud.NProto.TThrottlerInfo
	(*protos1.TTraceRequest)(nil), // 5: NLWTrace.TTraceRequest
	(protos.ERequestSource)(0),    // 6: NCloud.NProto.ERequestSource
}
var file_cloud_filestore_public_api_protos_headers_proto_depIdxs = []int32{
	2, // 0: NCloud.NFileStore.NProto.THeaders.Internal:type_name -> NCloud.NFileStore.NProto.THeaders.TInternal
	3, // 1: NCloud.NFileStore.NProto.TResponseHeaders.Trace:type_name -> NCloud.NProto.TTraceInfo
	4, // 2: NCloud.NFileStore.NProto.TResponseHeaders.Throttler:type_name -> NCloud.NProto.TThrottlerInfo
	5, // 3: NCloud.NFileStore.NProto.THeaders.TInternal.Trace:type_name -> NLWTrace.TTraceRequest
	6, // 4: NCloud.NFileStore.NProto.THeaders.TInternal.RequestSource:type_name -> NCloud.NProto.ERequestSource
	5, // [5:5] is the sub-list for method output_type
	5, // [5:5] is the sub-list for method input_type
	5, // [5:5] is the sub-list for extension type_name
	5, // [5:5] is the sub-list for extension extendee
	0, // [0:5] is the sub-list for field type_name
}

func init() { file_cloud_filestore_public_api_protos_headers_proto_init() }
func file_cloud_filestore_public_api_protos_headers_proto_init() {
	if File_cloud_filestore_public_api_protos_headers_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_cloud_filestore_public_api_protos_headers_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*THeaders); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_cloud_filestore_public_api_protos_headers_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TResponseHeaders); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_cloud_filestore_public_api_protos_headers_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*THeaders_TInternal); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_cloud_filestore_public_api_protos_headers_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   3,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_cloud_filestore_public_api_protos_headers_proto_goTypes,
		DependencyIndexes: file_cloud_filestore_public_api_protos_headers_proto_depIdxs,
		MessageInfos:      file_cloud_filestore_public_api_protos_headers_proto_msgTypes,
	}.Build()
	File_cloud_filestore_public_api_protos_headers_proto = out.File
	file_cloud_filestore_public_api_protos_headers_proto_rawDesc = nil
	file_cloud_filestore_public_api_protos_headers_proto_goTypes = nil
	file_cloud_filestore_public_api_protos_headers_proto_depIdxs = nil
}
