# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(1.28.85)

LICENSE(Apache-2.0)

PEERDIR(
    contrib/python/botocore
    contrib/python/jmespath
    contrib/python/s3transfer
)

NO_LINT()

PY_SRCS(
    TOP_LEVEL
    boto3/__init__.py
    boto3/compat.py
    boto3/docs/__init__.py
    boto3/docs/action.py
    boto3/docs/attr.py
    boto3/docs/base.py
    boto3/docs/client.py
    boto3/docs/collection.py
    boto3/docs/docstring.py
    boto3/docs/method.py
    boto3/docs/resource.py
    boto3/docs/service.py
    boto3/docs/subresource.py
    boto3/docs/utils.py
    boto3/docs/waiter.py
    boto3/dynamodb/__init__.py
    boto3/dynamodb/conditions.py
    boto3/dynamodb/table.py
    boto3/dynamodb/transform.py
    boto3/dynamodb/types.py
    boto3/ec2/__init__.py
    boto3/ec2/createtags.py
    boto3/ec2/deletetags.py
    boto3/exceptions.py
    boto3/resources/__init__.py
    boto3/resources/action.py
    boto3/resources/base.py
    boto3/resources/collection.py
    boto3/resources/factory.py
    boto3/resources/model.py
    boto3/resources/params.py
    boto3/resources/response.py
    boto3/s3/__init__.py
    boto3/s3/inject.py
    boto3/s3/transfer.py
    boto3/session.py
    boto3/utils.py
)

RESOURCE_FILES(
    PREFIX contrib/python/boto3/py3/
    .dist-info/METADATA
    .dist-info/top_level.txt
    boto3/data/cloudformation/2010-05-15/resources-1.json
    boto3/data/cloudwatch/2010-08-01/resources-1.json
    boto3/data/dynamodb/2012-08-10/resources-1.json
    boto3/data/ec2/2014-10-01/resources-1.json
    boto3/data/ec2/2015-03-01/resources-1.json
    boto3/data/ec2/2015-04-15/resources-1.json
    boto3/data/ec2/2015-10-01/resources-1.json
    boto3/data/ec2/2016-04-01/resources-1.json
    boto3/data/ec2/2016-09-15/resources-1.json
    boto3/data/ec2/2016-11-15/resources-1.json
    boto3/data/glacier/2012-06-01/resources-1.json
    boto3/data/iam/2010-05-08/resources-1.json
    boto3/data/opsworks/2013-02-18/resources-1.json
    boto3/data/s3/2006-03-01/resources-1.json
    boto3/data/sns/2010-03-31/resources-1.json
    boto3/data/sqs/2012-11-05/resources-1.json
    boto3/examples/cloudfront.rst
    boto3/examples/s3.rst
)

END()
